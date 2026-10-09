#!/usr/bin/env python3
"""Generate the Lynxer <-> raylib binding from raylib's own API description.

Input:  codegen/raylib_api.json  (raylib's tools/rlparser output, vendored)
Output: rust/raylib/src/generated_ops.rs  (Rust shims + OPS/CONSTANTS tables)
        stdlib/raylib.lynx                (the Lynxer stdlib wrapper, generated)

The Lynxer native-module ABI passes only int64 / float64 / cstring / bytes, so
every raylib function gets a small `extern "C"` shim that converts.

* Structs whose fields are all scalars (Vector2, Color, Rectangle, Camera3D, ...)
  are passed **by value, flattened into scalar arguments**.
* Every other struct (Image, Model, Sound, ...) is an **opaque int64 handle**.
  Struct *returns* are always a handle.
* Handles come from `<struct>New(...)` and are released with `<struct>Free(h)`;
  scalar fields get `<struct>Get<Field>` / `<struct>Set<Field>`.
* Enum values and integer defines are registered and exposed as `<camel>()`.

Skipped (documented in README): varargs (`TraceLog`, `TextFormat`) and anything
taking a function pointer (the `*Callback` family).
"""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
API = ROOT / "codegen" / "raylib_api.json"

SCALAR = {
    "int": "c_int",
    "unsigned int": "c_uint",
    "long": "c_long",
    "unsigned long": "c_ulong",
    "char": "c_char",
    "unsigned char": "u8",
    "signed char": "i8",
    "float": "f32",
    "double": "f64",
    "bool": "bool",
}

KEYWORDS = {
    "as", "break", "const", "continue", "crate", "dyn", "else", "enum", "extern",
    "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod",
    "move", "mut", "pub", "ref", "return", "self", "static", "struct", "super",
    "trait", "true", "type", "unsafe", "use", "where", "while", "async", "await",
    "box", "abstract", "become", "do", "final", "macro", "override", "priv",
    "typeof", "unsized", "virtual", "yield", "try", "union",
}


def load_api():
    """raylib's rlparser output has unescaped quotes inside some descriptions;
    blank those lines out before parsing."""
    lines = []
    for line in API.read_text().splitlines():
        if '"description"' in line:
            indent = line[: len(line) - len(line.lstrip())]
            comma = "," if line.rstrip().endswith(",") else ""
            lines.append(indent + '"description": ""' + comma)
        else:
            lines.append(line)
    return json.loads("\n".join(lines))


def parse_type(c_type):
    """('Vector2', 1, True) for 'const Vector2 *'; ('int', 0, False) for 'int'."""
    t = c_type.strip()
    is_const = t.startswith("const ")
    core = t[len("const "):] if is_const else t
    depth = core.count("*")
    return core.replace("*", "").strip(), depth, is_const


def lower_first(name):
    return name[:1].lower() + name[1:]


def upper_first(name):
    return name[:1].upper() + name[1:]


def rust_field(name):
    return name + "_" if name in KEYWORDS else name


def camel(name):
    words = [w for w in re.split(r"[^0-9a-zA-Z]+", name) if w]
    if not words:
        return name
    return words[0].lower() + "".join(w[:1].upper() + w[1:].lower() for w in words[1:])


def field_path(segs):
    return ".".join(rust_field(s) for s in segs)


def arg_name(segs):
    return "_".join(segs)


def tag_name(segs):
    return "".join(upper_first(s) for s in segs)


class Gen:
    def __init__(self, api):
        self.funcs = api["functions"]
        self.structs = {s["name"]: s.get("fields") or [] for s in api["structs"]}
        self.enums = api["enums"]
        self.defines = api.get("defines") or []
        self.callbacks = {c["name"] for c in (api.get("callbacks") or [])}
        self.aliases = {a["name"]: a["type"] for a in (api.get("aliases") or [])}
        self.ops = []
        self.shims = []
        self.wrappers = []
        self.constants = []
        self.skipped = []
        self._flat = {}

    def canon(self, name):
        return self.aliases.get(name, name)

    def is_flat(self, name, seen=()):
        name = self.canon(name)
        if name in self._flat:
            return self._flat[name]
        if name in seen or name not in self.structs:
            return False
        for f in self.structs[name]:
            base, depth, _ = parse_type(f["type"])
            cb = self.canon(base)
            if depth or base == "char":
                return False
            if base in SCALAR:
                continue
            if cb in self.structs and self.is_flat(cb, seen + (name,)):
                continue
            return False
        self._flat[name] = True
        return True

    def leaves(self, name, prefix=()):
        """Scalar leaves as (segments, c_type), through flat struct fields."""
        name = self.canon(name)
        out = []
        for f in self.structs[name]:
            base, depth, _ = parse_type(f["type"])
            cb = self.canon(base)
            segs = prefix + (f["name"],)
            if depth:
                continue
            if base in SCALAR:
                out.append((segs, base))
            elif cb in self.structs and self.is_flat(cb):
                out.extend(self.leaves(cb, segs))
        return out

    def rust_type(self, c_type):
        base, depth, is_const = parse_type(c_type)
        inner = SCALAR.get(base) or (f"rl::{base}" if self.canon(base) in self.structs else "c_void")
        for _ in range(depth):
            inner = ("*const " if is_const else "*mut ") + inner
        return inner

    def abi_token(self, c_type):
        base, depth, _ = parse_type(c_type)
        if base == "char" and depth == 1:
            return "cstring"
        if base in ("float", "double") and depth == 0:
            return "float64"
        return "int64"

    @staticmethod
    def shim_type(token):
        return {"cstring": "*const c_char", "float64": "f64", "int64": "i64"}[token]

    @staticmethod
    def lynx(token):
        return {"int64": "int", "float64": "float", "cstring": "str", "void": "void"}[token]

    def call_expr(self, c_type, arg):
        base, depth, is_const = parse_type(c_type)
        cb = self.canon(base)
        if depth == 0:
            if base == "float":
                return f"{arg} as f32"
            if base == "double":
                return f"{arg} as f64"
            if base == "bool":
                return f"{arg} != 0"
            if cb in self.structs:
                return f"std::ptr::read({arg} as *const rl::{base})"
            return f"{arg} as {SCALAR.get(base, 'c_int')}"
        if base == "char" and depth == 1:
            return arg if is_const else f"{arg} as *mut c_char"
        return f"{arg} as {self.rust_type(c_type)}"

    def flat_literal(self, orig, arg):
        """Rebuild a flat struct from flattened shim args (arg_<leaf>)."""
        fields = []
        for f in self.structs[self.canon(orig)]:
            base, depth, _ = parse_type(f["type"])
            if depth:
                continue
            a = f"{arg}_{f['name']}"
            if base in SCALAR:
                fields.append(f"{rust_field(f['name'])}: {a} as {SCALAR[base]}")
            elif self.canon(base) in self.structs:
                fields.append(f"{rust_field(f['name'])}: {self.flat_literal(base, a)}")
        return f"rl::{orig} {{ " + ", ".join(fields) + " }"

    def gen_function(self, f):
        name = f["name"]
        params = f.get("params") or []
        if any(p["type"] == "..." for p in params):
            self.skipped.append(name + " (varargs)")
            return
        if any(self.canon(parse_type(p["type"])[0]) in self.callbacks for p in params):
            self.skipped.append(name + " (callback)")
            return

        shim_args, tokens, call_args, lynx_args = [], [], [], []
        for i, p in enumerate(params):
            arg = f"a{i}"
            base, depth, _ = parse_type(p["type"])
            cb = self.canon(base)
            if depth == 0 and cb in self.structs and self.is_flat(cb):
                for segs, ltype in self.leaves(cb):
                    tok = "float64" if ltype in ("float", "double") else "int64"
                    shim_args.append(f"{arg}_{arg_name(segs)}: {self.shim_type(tok)}")
                    tokens.append(tok)
                    lynx_args.append((f"{arg}_{arg_name(segs)}", tok))
                call_args.append(self.flat_literal(base, arg))
                continue
            tok = self.abi_token(p["type"])
            shim_args.append(f"{arg}: {self.shim_type(tok)}")
            tokens.append(tok)
            lynx_args.append((arg, tok))
            call_args.append(self.call_expr(p["type"], arg))

        ret = f.get("returnType") or "void"
        rbase, rdepth, _ = parse_type(ret)
        rcb = self.canon(rbase)
        call = f"rl::{name}({', '.join(call_args)})"
        if rbase == "void":
            rtok, decl, body = "void", "", f"    {call};\n"
        elif rdepth == 0 and rcb in self.structs:
            rtok, decl, body = "int64", " -> i64", f"    let v = {call};\n    Box::into_raw(Box::new(v)) as i64\n"
        elif rbase == "char" and rdepth == 1:
            rtok, decl, body = "cstring", " -> *const c_char", f"    {call}\n"
        elif rdepth == 0 and rbase in ("float", "double"):
            rtok, decl, body = "float64", " -> f64", f"    let v = {call};\n    v as f64\n"
        else:
            rtok, decl, body = "int64", " -> i64", f"    let v = {call};\n    v as i64\n"

        sym = "rl_" + name
        self.shims.append(
            f'#[no_mangle]\npub unsafe extern "C" fn {sym}({", ".join(shim_args)}){decl} {{\n{body}}}\n'
        )
        self.ops.append((lower_first(name), sym, f"cdecl:{rtok}({','.join(tokens)})"))
        ln = lower_first(name)
        ln_args = ", ".join(f"{self.lynx(t)} {a}" for a, t in lynx_args)
        ln_call = ", ".join(a for a, _ in lynx_args)
        if rtok == "void":
            self.wrappers.append(f"global {ln}({ln_args}) {{ global.nativeRaylib.{ln}({ln_call}); }}")
        else:
            self.wrappers.append(
                f"global {ln}({ln_args}) -> {self.lynx(rtok)} {{ return global.nativeRaylib.{ln}({ln_call}); }}"
            )

    def gen_struct(self, name):
        low = lower_first(name)
        leaves = self.leaves(name)

        argdecl = ", ".join(
            f'a_{arg_name(segs)}: {"f64" if lt in ("float", "double") else "i64"}'
            for segs, lt in leaves
        )
        assigns = "\n".join(
            (f"    v.{field_path(segs)} = a_{arg_name(segs)} != 0;" if lt == "bool"
             else f"    v.{field_path(segs)} = a_{arg_name(segs)} as {SCALAR[lt]};")
            for segs, lt in leaves
        )
        sym = "rlnew_" + name
        self.shims.append(
            f'#[no_mangle]\npub unsafe extern "C" fn {sym}({argdecl}) -> i64 {{\n'
            f"    let mut v: rl::{name} = std::mem::zeroed();\n{assigns}\n"
            f"    Box::into_raw(Box::new(v)) as i64\n}}\n"
        )
        toks = ",".join("float64" if lt in ("float", "double") else "int64" for _, lt in leaves)
        self.ops.append((f"{low}New", sym, f"cdecl:int64({toks})"))
        ln_args = ", ".join(
            f'{self.lynx("float64" if lt in ("float", "double") else "int64")} {arg_name(segs)}'
            for segs, lt in leaves
        )
        ln_call = ", ".join(arg_name(segs) for segs, _ in leaves)
        self.wrappers.append(f"global {low}New({ln_args}) -> int {{ return global.nativeRaylib.{low}New({ln_call}); }}")

        sym = "rlfree_" + name
        self.shims.append(
            f'#[no_mangle]\npub unsafe extern "C" fn {sym}(h: i64) {{\n'
            f"    if h != 0 {{ drop(Box::from_raw(h as *mut rl::{name})); }}\n}}\n"
        )
        self.ops.append((f"{low}Free", sym, "cdecl:void(int64)"))
        self.wrappers.append(f"global {low}Free(int handle) {{ global.nativeRaylib.{low}Free(handle); }}")

        for segs, lt in leaves:
            path = field_path(segs)
            tag = tag_name(segs)
            base = arg_name(segs)
            self.shims.append(
                (f'#[no_mangle]\npub unsafe extern "C" fn rlget_{name}_{base}(h: i64) -> f64 {{\n'
                 f"    if (*(h as *const rl::{name})).{path} {{ 1.0 }} else {{ 0.0 }}\n}}\n"
                 if lt == "bool" else
                 f'#[no_mangle]\npub unsafe extern "C" fn rlget_{name}_{base}(h: i64) -> f64 {{\n'
                 f"    (*(h as *const rl::{name})).{path} as f64\n}}\n")
            )
            self.shims.append(
                (f'#[no_mangle]\npub unsafe extern "C" fn rlset_{name}_{base}(h: i64, value: f64) {{\n'
                 f"    (*(h as *mut rl::{name})).{path} = value != 0.0;\n}}\n"
                 if lt == "bool" else
                 f'#[no_mangle]\npub unsafe extern "C" fn rlset_{name}_{base}(h: i64, value: f64) {{\n'
                 f"    (*(h as *mut rl::{name})).{path} = value as {SCALAR[lt]};\n}}\n")
            )
            self.ops.append((f"{low}Get{tag}", f"rlget_{name}_{base}", "cdecl:float64(int64)"))
            self.ops.append((f"{low}Set{tag}", f"rlset_{name}_{base}", "cdecl:void(int64,float64)"))
            self.wrappers.append(f"global {low}Get{tag}(int handle) -> float {{ return global.nativeRaylib.{low}Get{tag}(handle); }}")
            self.wrappers.append(f"global {low}Set{tag}(int handle, float value) {{ global.nativeRaylib.{low}Set{tag}(handle, value); }}")

    def gen_constants(self):
        seen = set()
        for e in self.enums:
            for v in e.get("values") or []:
                if v["name"] not in seen:
                    seen.add(v["name"])
                    self.constants.append((v["name"], int(v["value"])))
        for d in self.defines:
            if d.get("type") == "INT" and isinstance(d.get("value"), int) and d["name"] not in seen:
                seen.add(d["name"])
                self.constants.append((d["name"], int(d["value"])))
        for cname, cvalue in self.constants:
            gname = camel(cname)
            sym = "rlconst_" + cname
            self.shims.append(f'#[no_mangle]\npub unsafe extern "C" fn {sym}() -> i64 {{ {cvalue} }}\n')
            self.ops.append((gname, sym, "cdecl:int64()"))
            self.wrappers.append(f"global {gname}() -> int {{ return global.nativeRaylib.{gname}(); }}")

    def run(self):
        for f in self.funcs:
            self.gen_function(f)
        for name in self.structs:
            self.gen_struct(name)
        self.gen_constants()

        header = (
            "// GENERATED by codegen/generate.py from raylib_api.json - do not edit.\n"
            f"// {len(self.ops)} ops, {len(self.constants)} constants.\n\n"
        )
        ops_rs = "pub static OPS: &[(&str, &str, &str)] = &[\n"
        ops_rs += "".join(f'    ("{n}", "{s}", "{g}"),\n' for n, s, g in self.ops)
        ops_rs += "];\n\npub static CONSTANTS: &[(&str, i64)] = &[\n"
        ops_rs += "".join(f'    ("{n}", {v}),\n' for n, v in self.constants)
        ops_rs += "];\n\n"
        (ROOT / "src" / "generated_ops.rs").write_text(header + ops_rs + "\n".join(self.shims))

        lynx = [
            "////",
            "Lynxer standard library: raylib.",
            "Bindings for (almost) every raylib 6.0 function, generated from raylib's own",
            "API description by `lynxer/rust/raylib/codegen/generate.py` and backed by the",
            "Rust crate `lynxer_raylib` (`lynxer/rust/raylib`).",
            "",
            "Every raylib function keeps its name, lower-camel-cased (DrawText ->",
            "drawText), and raylib's enum values and #defines are exposed as getters",
            "(flagWindowHidden(), keySpace()). Scalar-only structs (Vector2/3/4, Color,",
            "Rectangle, Camera3D, Ray, BoundingBox, Texture, ...) are passed by value as",
            "flattened scalars; every other struct (Image, Model, Sound, Music, Font, ...)",
            "is an opaque int handle made with <struct>New, read/written with",
            "<struct>Get<Field>/<struct>Set<Field>, and released with <struct>Free.",
            "Requires a Rust toolchain (cargo) to build the backend; see docs/stdlib/raylib.md.",
            "////",
            "",
            "global setup(){",
            '    importAs("raylib.so", "nativeRaylib");',
            "}",
            "",
        ] + self.wrappers + [""]
        (ROOT.parent.parent / "stdlib" / "raylib.lynx").write_text("\n".join(lynx))

        print(f"ops={len(self.ops)} constants={len(self.constants)} skipped={len(self.skipped)}")
        for s in self.skipped:
            print("  skip:", s)


if __name__ == "__main__":
    main_api = load_api()
    Gen(main_api).run()
