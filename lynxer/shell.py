#!/usr/bin/env python3
"""CLI entry point for Lynxer. Run with: python lynxer/shell.py <file.lynx>"""

import os
import subprocess
import sys
import time

_here = os.path.dirname(os.path.abspath(__file__))  # .../lynxer/
_parent = os.path.dirname(_here)  # repo root
if _parent not in sys.path:
    sys.path.insert(0, _parent)

from lynxer import compile_to_bytecode, run, run_bytecode
from lynxer.bundle import bundle_program
from lynxer.bytecode import BYTECODE_VERSION, read_bytecode
from lynxer.formatting import FormattingError, format_source, lint_source
from lynxer.install import installer_main
from lynxer.lynxer import Lexer, Parser, Token, stdlib_dir


def _extract_docstring(path):
    """Return the text inside the first //// ... //// block in a Lynxer file, or None."""
    lines = []
    inside = False
    try:
        with open(path, "r", encoding="utf-8") as fh:
            for line in fh:
                stripped = line.strip()
                if not inside:
                    if stripped == "////":
                        inside = True
                else:
                    if stripped == "////":
                        break
                    lines.append(line.rstrip())
    except Exception as e:  # noqa: BLE001
        print(f"Error: could not read '{path}': {e}")
        pass
    text = "\n".join(lines).strip()
    return text if text else None


def _view_bytecode(filepath):
    """Pretty-print the metadata and top-level structure of a .lynxc file."""
    try:
        data, raw_size, stored_size = read_bytecode(filepath)
    except (OSError, ValueError) as exc:
        print(f"Error: could not read '{filepath}': {exc}", file=sys.stderr)
        return 1

    file_ver = data.get("version", "<unknown>")
    source = data.get("source", "<unknown>")
    node = data.get("node")

    ver_ok = (
        "✓"
        if file_ver == BYTECODE_VERSION
        else "✗ (runtime expects v{})".format(BYTECODE_VERSION)
    )

    print("Lynxer Bytecode Inspector")
    print("─" * 44)
    print(f"  File   : {filepath}")
    print(f"  Source : {source}")
    print(f"  Version: {file_ver}  {ver_ok}")
    print(
        f"  Size   : {raw_size:,} bytes (decompressed), {stored_size:,} bytes (stored)"
    )
    print("─" * 44)

    if node is None:
        print("  (no AST node stored)")
        return 0

    # Collect top-level globals from the AST
    globals_list = getattr(node, "globals_list", [])
    setup_func = getattr(node, "setup_func", None)
    main_func = None

    print()
    print("  Top-level globals:")
    if not globals_list:
        print("    (none)")
    for fn_def in globals_list:
        fname = getattr(fn_def, "var_name_tok", None)
        params = getattr(fn_def, "param_toks", [])
        name = fname.value if fname else "?"
        param_strs = []
        for param in params:
            pt, nt = param[:2]
            type_str = pt.value if pt else "any"
            default_str = " = ..." if len(param) > 2 and param[2] is not None else ""
            param_strs.append(f"{type_str} {nt.value}{default_str}")
        is_async = getattr(fn_def, "is_async", False)
        prefix = "async " if is_async else ""
        print(f"    {prefix}global {name}({', '.join(param_strs)})")

        # One level of nesting
        body = getattr(fn_def, "body_block", None)
        if body:
            for inner in getattr(body, "statements", []):
                iname = getattr(inner, "var_name_tok", None)
                if iname is None:
                    continue
                iparams = getattr(inner, "param_toks", [])
                ip_strs = []
                for param in iparams:
                    pt, nt = param[:2]
                    type_str = pt.value if pt else "any"
                    default_str = (
                        " = ..." if len(param) > 2 and param[2] is not None else ""
                    )
                    ip_strs.append(f"{type_str} {nt.value}{default_str}")
                ia = getattr(inner, "is_async", False)
                ipfx = "async " if ia else ""
                print(f"      {ipfx}global {iname.value}({', '.join(ip_strs)})")

    if setup_func:
        print()
        print("  setup() : present")
    # scan globals for main
    for fn_def in globals_list:
        fname = getattr(fn_def, "var_name_tok", None)
        if fname and fname.value == "main":
            main_func = fn_def
            break
    if main_func:
        print("  main()  : present")

    return 0


def _ast_lines(value, indent=0):
    """Render parser nodes as a readable, position-free tree."""
    prefix = " " * indent

    if isinstance(value, Token):
        return [f"{prefix}Token(type={value.type!r}, value={value.value!r})"]
    if value is None or isinstance(value, (str, int, float, bool)):
        return [f"{prefix}{value!r}"]
    if isinstance(value, (list, tuple)):
        if not value:
            return [f"{prefix}{type(value).__name__}[]"]
        lines = [f"{prefix}{type(value).__name__}["]
        for item in value:
            lines.extend(_ast_lines(item, indent + 2))
        lines.append(f"{prefix}]")
        return lines
    if isinstance(value, dict):
        if not value:
            return [f"{prefix}{{}}"]
        lines = [f"{prefix}{{"]
        for key, item in value.items():
            child = _ast_lines(item, indent + 4)
            lines.append(f"{' ' * (indent + 2)}{key!r}:")
            lines.extend(child)
        lines.append(f"{prefix}}}")
        return lines
    if hasattr(value, "__dict__"):
        lines = [f"{prefix}{type(value).__name__}"]
        for name, item in vars(value).items():
            if name in {"pos_start", "pos_end"}:
                continue
            child = _ast_lines(item, indent + 4)
            lines.append(f"{' ' * (indent + 2)}{name}:")
            lines.extend(child)
        return lines
    return [f"{prefix}{value!r}"]


def _print_ast(filepath, source):
    """Parse and print a Lynxer source AST without executing it."""
    lexer = Lexer(filepath, source)
    tokens, error = lexer.make_tokens()
    if error:
        print(error.as_string(), file=sys.stderr)
        return 1

    result = Parser(tokens).parse()
    if result.error:
        print(result.error.as_string(), file=sys.stderr)
        return 1

    print("Lynxer AST")
    print("===========")
    print("\n".join(_ast_lines(result.node)))
    return 0


def main():
    argv = sys.argv[1:]
    if not argv or argv[0] in ("-h", "--help"):
        print()
        print("Usage:")
        print("  lynxer <file.lynx>                    Run a Lynxer source file")
        print("  lynxer --compile <file.lynx>          Compile to bytecode (.lynxc)")
        print(
            "  lynxer --compile --no-cache <file>    Recompile even when bytecode is current"
        )
        print(
            "  lynxer --compile --no-opt <file>      Compile without optimization passes"
        )
        print("  lynxer --bundle <file.lynx> [name]     Build a standalone native executable")
        print("  lynxer <file.lynxc>                   Run a compiled bytecode file")
        print(
            "  lynxer --view-bytecode <file.lynxc>   Inspect bytecode metadata and structure"
        )
        print(
            "  lynxer --ast <file.lynx>              Parse and print the abstract syntax tree"
        )
        print(
            "  lynxer --benchmark-compile <files...> Benchmark optimized and unoptimized compilation"
        )
        print(
            "  lynxer --format <file.lynx>           Format a Lynxer source file in place"
        )
        print(
            "  lynxer --format-oneline <file.lynx>   Compact a Lynxer source file to one line"
        )
        print(
            "  lynxer --lint <file.lynx>             Check Lynxer syntax without running it"
        )
        print(
            "  lynxer --validate-executeable         Run the comprehensive interpreter validator"
        )
        print("  lynxer --version                      Print version")
        print(
            "  lynxer --list-stdlibs                 List available Lynxer stdlib modules"
        )
        print(
            "  lynxer --install                      Install the compiled executable as /usr/bin/lynxer, may require sudo"
        )
        print(
            "  lynxer --uninstall                    Remove /usr/bin/lynxer, also may require sudo"
        )
        print()
        print(
            "  BTW, please run the install and uninstall with the executeable, not shell.py nor anything else."
        )
        print(
            "  If you are running from source, use the compiled executable instead located in GitHub Releases."
        )
        print()
        return 0
    if argv[0] in ("-v", "--version", "-version", "--v"):
        print("Lynxer 0.1.8")
        return 0
    if argv[0] in ("--validate-executeable", "--validate-executable"):
        validator = os.path.join(_here, "validate.py")
        if not os.path.exists(validator):
            print("shell.py: comprehensive validator is not available", file=sys.stderr)
            return 1
        result = subprocess.run(
            [sys.executable, validator, *argv[1:]], cwd=_parent, check=False
        )
        return result.returncode
    if argv[0] in ("--install", "--uninstall"):
        return installer_main(argv[0])
    if argv[0] in (
        "-easterEgg",
        "--easterEgg",
        "--idklmao",
        "-wnwnerbcyunwrbygnubeuyxnqybxun",
    ):
        print("Easter Egg found!")
        print("Wanna do sudo rm -rf / --no-preserve-root? Just kidding, don't do that.")
        print("But seriously, don't do that. It's a bad idea.")
        print(
            "That will erase the entire linux OS and all your files. You will lose everything."
        )
        return 0

    if argv[0] in ("--list-stdlibs", "--stdlibs", "-stdlibs", "-list-stdlibs"):
        stdlib_path = stdlib_dir()
        if not os.path.isdir(stdlib_path):
            print("No stdlib directory found.")
            return 1
        files = sorted(
            f for f in os.listdir(stdlib_path) if f.endswith(".lynx")
        )
        if not files:
            print("No Lynxer stdlib modules found.")
            return 0
        print("Available Lynxer stdlib modules:\n")
        for fn in files:
            path = os.path.join(stdlib_path, fn)
            name = os.path.splitext(fn)[0]
            docstring = _extract_docstring(path)
            if docstring:
                # Print the module name as a header, then the docstring indented
                print(f"  {name}")
                for line in docstring.splitlines():
                    print(f"    {line}" if line.strip() else "")
                print()
            else:
                print(f"  {name}\n")
        return 0

    if argv[0] in ("--benchmark-compile", "--bench-compile"):
        if len(argv) < 2:
            print(
                "shell.py: --benchmark-compile requires at least one .lynx file",
                file=sys.stderr,
            )
            return 1
        failures = 0
        print("file,mode,elapsed_ms,status")
        for raw_path in argv[1:]:
            src = raw_path if os.path.isabs(raw_path) else os.path.join(os.getcwd(), raw_path)
            try:
                with open(src, "r", encoding="utf-8") as fh:
                    source = fh.read()
            except (OSError, UnicodeError) as exc:
                failures += 1
                print(f"{raw_path},read,0,error:{exc}")
                continue
            for optimize in (False, True):
                started = time.perf_counter()
                _, error = compile_to_bytecode(
                    src,
                    source,
                    optimize=optimize,
                    use_cache=False,
                )
                elapsed_ms = int((time.perf_counter() - started) * 1000)
                mode = "optimized" if optimize else "unoptimized"
                if error:
                    failures += 1
                    detail = error.details.replace(",", ";")
                    print(f"{raw_path},{mode},{elapsed_ms},error:{detail}")
                else:
                    print(f"{raw_path},{mode},{elapsed_ms},ok")
        return 1 if failures else 0

    if argv[0] in ("--ast", "--format", "--format-oneline", "--lint"):
        if len(argv) != 2:
            print(
                f"shell.py: {argv[0]} requires exactly one file argument",
                file=sys.stderr,
            )
            return 1

        source_path = argv[1]
        if not os.path.isabs(source_path):
            source_path = os.path.join(os.getcwd(), source_path)
        if not os.path.exists(source_path):
            print(f"shell.py: file not found: '{argv[1]}'", file=sys.stderr)
            return 1

        try:
            with open(source_path, "r", encoding="utf-8") as source_file:
                source = source_file.read()
        except OSError as exc:
            print(f"shell.py: could not read '{argv[1]}': {exc}", file=sys.stderr)
            return 1

        if argv[0] == "--ast":
            return _print_ast(source_path, source)

        if argv[0] == "--lint":
            error = lint_source(source_path, source)
            if error:
                print(error.as_string(), file=sys.stderr)
                return 1
            print(f"Lint OK: {argv[1]}")
            return 0

        try:
            formatted = format_source(
                source_path,
                source,
                oneline=argv[0] == "--format-oneline",
            )
            with open(source_path, "w", encoding="utf-8", newline="") as output_file:
                output_file.write(formatted)
        except (FormattingError, OSError) as exc:
            error = exc.error if isinstance(exc, FormattingError) else None
            if error is not None:
                print(error.as_string(), file=sys.stderr)
            else:
                print(f"shell.py: could not format '{argv[1]}': {exc}", file=sys.stderr)
            return 1

        print(f"Formatted: {argv[1]}")
        return 0

    if argv[0] in ("-c", "--compile", "--c", "-compile"):
        compile_args = list(argv[1:])
        optimize = True
        use_cache = True
        if "--no-opt" in compile_args:
            optimize = False
            compile_args.remove("--no-opt")
        if "--no-cache" in compile_args:
            use_cache = False
            compile_args.remove("--no-cache")
        if len(compile_args) != 1:
            print("shell.py: --compile requires a file argument", file=sys.stderr)
            return 1
        src = compile_args[0]
        if not os.path.isabs(src):
            src = os.path.join(os.getcwd(), src)
        if not os.path.exists(src):
            print(f"shell.py: file not found: '{compile_args[0]}'", file=sys.stderr)
            return 1
        try:
            with open(src, "r", encoding="utf-8") as fh:
                source = fh.read()
        except (OSError, UnicodeError) as exc:
            print(f"shell.py: could not read '{compile_args[0]}': {exc}", file=sys.stderr)
            return 1
        out_path, error = compile_to_bytecode(src, source, optimize=optimize, use_cache=use_cache)
        if error:
            print(error.as_string(), file=sys.stderr)
            return 1
        print(f"Compiled: {out_path}")
        return 0

    if argv[0] in ("--bundle", "-bundle"):
        if len(argv) not in (2, 3):
            print("shell.py: --bundle requires a .lynx file and optional output name", file=sys.stderr)
            return 1
        try:
            executable = bundle_program(argv[1], argv[2] if len(argv) == 3 else None)
        except (OSError, RuntimeError, UnicodeError) as exc:
            print(f"shell.py: bundle failed: {exc}", file=sys.stderr)
            return 1
        print(f"Bundled: {executable}")
        return 0

    if argv[0] in ("--view-bytecode", "--inspect-bytecode", "--disasm"):
        if len(argv) < 2:
            print(
                "shell.py: --view-bytecode requires a .lynxc file argument",
                file=sys.stderr,
            )
            return 1
        src = argv[1]
        if not os.path.isabs(src):
            src = os.path.join(os.getcwd(), src)
        if not os.path.exists(src):
            print(f"shell.py: file not found: '{argv[1]}'", file=sys.stderr)
            return 1
        return _view_bytecode(src)

    filepath = argv[0]
    if not os.path.isabs(filepath):
        filepath = os.path.join(os.getcwd(), filepath)

    if not os.path.exists(filepath):
        print(f"shell.py: file not found: '{argv[0]}'", file=sys.stderr)
        return 1

    # Run pre-compiled bytecode directly
    if filepath.endswith(".lynxc"):
        try:
            _, error = run_bytecode(filepath)
        except Exception as exc:  # noqa: BLE001
            print(
                f"shell.py: could not run bytecode '{argv[0]}': {exc}", file=sys.stderr
            )
            return 1
        if error:
            print(error.as_string(), file=sys.stderr)
            return 1
        return 0

    try:
        with open(filepath, "r", encoding="utf-8") as fh:
            source = fh.read()
    except (OSError, UnicodeError) as exc:
        print(f"shell.py: could not read '{argv[0]}': {exc}", file=sys.stderr)
        return 1

    try:
        _, error = run(filepath, source)
    except Exception as exc:  # noqa: BLE001
        print(f"shell.py: interpreter failure in '{argv[0]}': {exc}", file=sys.stderr)
        return 1
    if error:
        print(error.as_string(), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        # Ctrl-C is an intentional CLI exit, not an interpreter error.
        print()
        sys.exit(130)
