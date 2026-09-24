#!/usr/bin/env python3
"""Check that each stdlib `.lynx` wrapper agrees with its native backend.

Why this exists
---------------
A fixture asserts what a module *does*, so a module can be broken in a way its
own `.expected` file happily records. Two real defects shipped that way:

* `sqldb` passed a database path through the wrapper while the backend read
  argument 0 as an integer handle, so every call returned
  ``ERROR: invalid handle`` — and the fixture asserted exactly that.
* `tui` read packed arguments positionally (``args.string(1)`` for the second
  *argument* rather than the second *string*), which ``Args`` cannot detect
  because an out-of-range read silently yields ``0`` / ``""``.

Both are contract violations between two files that sit side by side, and both
are invisible at run time. This script compares them statically.

What it checks
--------------
For every ``stdlib/<name>.lynx`` that imports ``stdlib/<name>.so``:

1. **Op coverage** — every ``global.native<Alias>.<op>(...)`` call in the
   wrapper names an op the backend registers. (failure)
2. **Packed argument bounds** — for a Rust backend, where every op is
   registered with the packed ``cdecl:<ret>(...)`` signature, the highest index
   an op reads for a given argument kind must be lower than the number of
   arguments of that kind the wrapper passes. Numbers (``int``/``float``/
   ``bool``) and strings are counted separately, which is what ``lynxer_abi``
   delivers them as. (failure)
3. **Unused registrations** — an op the backend registers that no wrapper
   function calls. (warning)

C++ backends register one of the fixed shapes, which the interpreter already
type-checks per argument when the op is called (``native call argument count
does not match signature``), so only rule 1 applies to them.

A wrapper function whose argument kinds cannot be inferred (an expression, a
function call, an untyped parameter) is reported as *skipped* rather than
guessed at, so the check never fails on something it cannot read.

Usage
-----
    python3 lynxer/scripts/check_module_contracts.py [--verbose]

Exits 0 when no failures were found, 1 otherwise.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

LYNXER_ROOT = Path(__file__).resolve().parents[1]
STDLIB_DIR = LYNXER_ROOT / "stdlib"
RUST_DIR = LYNXER_ROOT / "rust"

# Argument kinds as `lynxer_abi` delivers them: numbers and strings travel in
# two independent lists, so indices are per kind.
NUMBER = "number"
STRING = "string"

NUMBER_TYPES = {
    "int",
    "float",
    "bool",
    "num",
    "numBool",
    "bit",
    "byte",
    "int8",
    "int16",
    "int32",
    "int64",
    "uint8",
    "uint16",
    "uint32",
    "uint64",
    "float32",
    "float64",
}

# Mirrors `Args` in lynxer/rust/abi/src/lib.rs: which list each accessor reads.
ACCESSOR_KIND = {
    "int": NUMBER,
    "float": NUMBER,
    "bool": NUMBER,
    "num": NUMBER,
    "string": STRING,
}

RETURN_KINDS = {"int64": NUMBER, "float64": NUMBER, "cstring": STRING}


class Finding:
    def __init__(self, path: Path, line: int, message: str, fatal: bool):
        self.path = path
        self.line = line
        self.message = message
        self.fatal = fatal

    def render(self) -> str:
        try:
            shown = self.path.relative_to(LYNXER_ROOT.parent)
        except ValueError:
            shown = self.path
        location = f"{shown}:{self.line}" if self.line > 0 else str(shown)
        return f"{location}: {self.message}"


def strip_line_comments(text: str) -> str:
    """Blank out `//`-to-end-of-line comments without touching string content."""
    out = []
    for line in text.splitlines(keepends=True):
        index = 0
        in_string = False
        escaped = False
        comment_at = None
        while index < len(line):
            character = line[index]
            if in_string:
                if escaped:
                    escaped = False
                elif character == "\\":
                    escaped = True
                elif character == '"':
                    in_string = False
            elif character == '"':
                in_string = True
            elif character == "/" and line.startswith("//", index):
                comment_at = index
                break
            index += 1
        if comment_at is None:
            out.append(line)
        else:
            out.append(line[:comment_at] + "\n")
    return "".join(out)


def match_brace(text: str, open_index: int) -> int:
    """Return the index of the `}` matching the `{` at `open_index`."""
    assert text[open_index] == "{"
    depth = 0
    index = open_index
    in_string = False
    escaped = False
    while index < len(text):
        character = text[index]
        if in_string:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
        elif character == '"':
            in_string = True
        elif character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    raise ValueError("unbalanced braces")


def split_top_level(text: str) -> list[str]:
    """Split on commas that are not inside parentheses, brackets or strings."""
    parts: list[str] = []
    current: list[str] = []
    depth = 0
    in_string = False
    escaped = False
    for character in text:
        if in_string:
            current.append(character)
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
            continue
        if character == '"':
            in_string = True
            current.append(character)
        elif character in "([{":
            depth += 1
            current.append(character)
        elif character in ")]}":
            depth -= 1
            current.append(character)
        elif character == "," and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(character)
    tail = "".join(current).strip()
    if tail:
        parts.append(tail)
    return parts


def line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


# --- Backends ---------------------------------------------------------------


class Backend:
    """Ops a native module registers, keyed by Lynxer-facing name."""

    def __init__(self, path: Path):
        self.path = path
        self.packed = False
        # op name -> {"symbol": str, "signature": str, "line": int}
        self.ops: dict[str, dict] = {}
        # symbol -> [(kind, index, line)] read from the packed argument view
        self.reads: dict[str, list[tuple[str, int, int]]] = {}


RUST_OPS_ENTRY = re.compile(
    r'\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"cdecl:([A-Za-z0-9]+)\(([^)]*)\)"\s*,?\s*\)'
)
RUST_EXPORT = re.compile(r"export_(int|float|string)!\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*args\s*,\s*\{")
ARG_READ = re.compile(r"args\.([A-Za-z_][A-Za-z0-9_]*)\(\s*(\d+)\s*\)")
# The registration callback is spelled differently per module (`function`, `f`,
# ...), so match the call shape rather than the callback's name.
CPP_OPS_ENTRY = re.compile(
    r'\b[A-Za-z_][A-Za-z0-9_]*\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"cdecl:([A-Za-z0-9]+)\(([^)]*)\)"'
)


def parse_rust_backend(path: Path) -> Backend:
    backend = Backend(path)
    text = strip_line_comments(path.read_text())
    for match in RUST_OPS_ENTRY.finditer(text):
        name, symbol, _return, params = match.groups()
        backend.ops[name] = {
            "symbol": symbol,
            "signature": params,
            "line": line_of(text, match.start()),
        }
        if params.strip() == "...":
            backend.packed = True

    for match in RUST_EXPORT.finditer(text):
        symbol = match.group(2)
        body_open = match.end() - 1
        body = text[body_open : match_brace(text, body_open)]
        body_line = line_of(text, body_open)
        found = []
        for read in ARG_READ.finditer(body):
            accessor, index = read.group(1), int(read.group(2))
            kind = ACCESSOR_KIND.get(accessor)
            if kind is None:
                continue
            found.append((kind, index, body_line + body.count("\n", 0, read.start())))
        backend.reads.setdefault(symbol, []).extend(found)
    return backend


def parse_cpp_backend(path: Path) -> Backend:
    backend = Backend(path)
    text = strip_line_comments(path.read_text())
    for match in CPP_OPS_ENTRY.finditer(text):
        name, symbol, _return, params = match.groups()
        backend.ops[name] = {
            "symbol": symbol,
            "signature": params,
            "line": line_of(text, match.start()),
        }
        if params.strip() == "...":
            backend.packed = True
    return backend


# --- Wrappers ---------------------------------------------------------------


class WrapperCall:
    def __init__(self, op: str, kinds: list[str | None], line: int):
        self.op = op
        self.kinds = kinds
        self.line = line


class Wrapper:
    def __init__(self, path: Path):
        self.path = path
        self.alias: str | None = None
        self.calls: list[WrapperCall] = []
        self.skipped: list[tuple[int, str]] = []


GLOBAL_FUNCTION = re.compile(r"\bglobal\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")
IMPORT_AS = re.compile(r'importAs\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)')


def parse_wrapper(path: Path, module: str) -> Wrapper:
    wrapper = Wrapper(path)
    text = strip_line_comments(path.read_text())

    for match in IMPORT_AS.finditer(text):
        library, alias = match.groups()
        if library == f"{module}.so":
            wrapper.alias = alias
    if wrapper.alias is None:
        return wrapper

    call_pattern = re.compile(
        r"\bglobal\." + re.escape(wrapper.alias) + r"\.([A-Za-z_][A-Za-z0-9_]*)\s*\("
    )

    for function in GLOBAL_FUNCTION.finditer(text):
        # Parameters run from the `(` to its matching `)`.
        params_open = function.end() - 1
        params_close = params_open
        depth = 0
        while params_close < len(text):
            if text[params_close] == "(":
                depth += 1
            elif text[params_close] == ")":
                depth -= 1
                if depth == 0:
                    break
            params_close += 1
        params_text = text[params_open + 1 : params_close]

        # The body is the next `{` and its match.
        body_open = text.find("{", params_close)
        if body_open == -1:
            continue
        body = text[body_open : match_brace(text, body_open)]

        param_kinds: dict[str, str] = {}
        for raw in split_top_level(params_text):
            if not raw:
                continue
            pieces = raw.split()
            if len(pieces) < 2:
                continue
            kind, name = pieces[0], pieces[-1]
            if kind == "str":
                param_kinds[name] = STRING
            elif kind in NUMBER_TYPES:
                param_kinds[name] = NUMBER

        for call in call_pattern.finditer(body):
            op = call.group(1)
            args_open = body.find("(", call.end() - 1)
            args_close = args_open
            depth = 0
            while args_close < len(body):
                if body[args_close] == "(":
                    depth += 1
                elif body[args_close] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                args_close += 1
            args_text = body[args_open + 1 : args_close]
            line = line_of(text, body_open + call.start())

            kinds: list[str | None] = []
            for raw in split_top_level(args_text):
                if not raw:
                    continue
                if raw in param_kinds:
                    kinds.append(param_kinds[raw])
                elif re.fullmatch(r"-?\d+(\.\d+)?", raw):
                    kinds.append(NUMBER)
                elif raw in ("true", "false"):
                    kinds.append(NUMBER)
                elif len(raw) >= 2 and raw.startswith('"') and raw.endswith('"'):
                    kinds.append(STRING)
                else:
                    kinds.append(None)
            wrapper.calls.append(WrapperCall(op, kinds, line))
    return wrapper


# --- Checks -----------------------------------------------------------------


def check_module(module: str, verbose: bool) -> tuple[list[Finding], int, int]:
    wrapper_path = STDLIB_DIR / f"{module}.lynx"
    rust_path = RUST_DIR / module / "src" / "lib.rs"
    cpp_path = STDLIB_DIR / f"{module}.cpp"

    if rust_path.is_file():
        backend = parse_rust_backend(rust_path)
    elif cpp_path.is_file():
        backend = parse_cpp_backend(cpp_path)
    else:
        return [], 0, 0

    wrapper = parse_wrapper(wrapper_path, module)
    if wrapper.alias is None:
        return [], 0, 0

    findings: list[Finding] = []
    skipped = 0
    checked = 0

    for call in wrapper.calls:
        entry = backend.ops.get(call.op)
        if entry is None:
            findings.append(
                Finding(
                    wrapper.path,
                    call.line,
                    f"wrapper calls '{call.op}', which the backend does not register",
                    fatal=True,
                )
            )
            continue
        if not backend.packed:
            checked += 1
            continue
        if any(kind is None for kind in call.kinds):
            skipped += 1
            if verbose:
                findings.append(
                    Finding(
                        wrapper.path,
                        call.line,
                        f"skipped '{call.op}': argument kinds could not be inferred",
                        fatal=False,
                    )
                )
            continue

        available = {
            NUMBER: sum(1 for kind in call.kinds if kind == NUMBER),
            STRING: sum(1 for kind in call.kinds if kind == STRING),
        }
        for kind, index, line in backend.reads.get(entry["symbol"], []):
            if index >= available[kind]:
                findings.append(
                    Finding(
                        rust_path,
                        line,
                        f"'{call.op}' reads args.{'string' if kind == STRING else 'int'}({index}), "
                        f"but the wrapper {wrapper.path.name} passes {available[kind]} "
                        f"{kind} argument(s) — the read is out of range and silently "
                        f"yields {'\"\"' if kind == STRING else '0'}",
                        fatal=True,
                    )
                )
        checked += 1

    called = {call.op for call in wrapper.calls}
    for name, entry in sorted(backend.ops.items()):
        if name not in called:
            findings.append(
                Finding(
                    backend.path,
                    entry["line"],
                    f"backend registers '{name}', but no function in "
                    f"{wrapper.path.name} calls it",
                    fatal=False,
                )
            )
    return findings, checked, skipped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="also report skipped wrapper calls and coverage details",
    )
    arguments = parser.parse_args()

    modules = sorted(
        path.stem
        for path in STDLIB_DIR.glob("*.lynx")
        if (RUST_DIR / path.stem).is_dir() or (STDLIB_DIR / f"{path.stem}.cpp").is_file()
    )

    failures: list[Finding] = []
    warnings: list[Finding] = []
    total_checked = 0
    total_skipped = 0

    for module in modules:
        findings, checked, skipped = check_module(module, arguments.verbose)
        total_checked += checked
        total_skipped += skipped
        for finding in findings:
            (failures if finding.fatal else warnings).append(finding)

    if arguments.verbose:
        for warning in warnings:
            print(f"warning: {warning.render()}")
        if warnings:
            print()

    for failure in failures:
        print(f"error: {failure.render()}", file=sys.stderr)

    print(
        f"module contracts: {len(modules)} backend(s), "
        f"{total_checked} op call(s) checked, {total_skipped} skipped, "
        f"{len(failures)} error(s), {len(warnings)} warning(s)"
    )

    if failures:
        print(
            "Packed arguments are indexed per kind: args.int(i) reads the i-th "
            "number and args.string(i) the i-th string. See "
            "docs/native-module-abi.md.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
