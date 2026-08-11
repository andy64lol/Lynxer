#!/usr/bin/env python3
"""CLI entry point for Lynxer. Run with: python lynxer/shell.py <file.lynx>"""
import sys
import os

_here   = os.path.dirname(os.path.abspath(__file__))   # .../lynxer/
_parent = os.path.dirname(_here)                         # repo root
if _parent not in sys.path:
    sys.path.insert(0, _parent)

from lynxer import run, compile_to_bytecode, run_bytecode  # noqa: E402
from lynxer.bytecode import BYTECODE_VERSION, read_bytecode  # noqa: E402
from lynxer.install import installer_main  # noqa: E402

def _extract_docstring(path):
    """Return the text inside the first //// ... //// block in a Lynxer file, or None."""
    lines = []
    inside = False
    try:
        with open(path, 'r', encoding='utf-8') as fh:
            for line in fh:
                stripped = line.strip()
                if not inside:
                    if stripped == '////':
                        inside = True
                else:
                    if stripped == '////':
                        break
                    lines.append(line.rstrip())
    except Exception as e:
        print(f"Error: could not read '{path}': {e}")
        pass
    text = '\n'.join(lines).strip()
    return text if text else None


def _view_bytecode(filepath):
    """Pretty-print the metadata and top-level structure of a .lynxc file."""
    try:
        data, raw_size, stored_size = read_bytecode(filepath)
    except (OSError, ValueError) as exc:
        print(f"Error: could not read '{filepath}': {exc}", file=sys.stderr)
        return 1

    file_ver  = data.get("version", "<unknown>")
    source    = data.get("source",  "<unknown>")
    node      = data.get("node")

    ver_ok = "✓" if file_ver == BYTECODE_VERSION else "✗ (runtime expects v{})".format(BYTECODE_VERSION)

    print("Lynxer Bytecode Inspector")
    print("─" * 44)
    print(f"  File   : {filepath}")
    print(f"  Source : {source}")
    print(f"  Version: {file_ver}  {ver_ok}")
    print(f"  Size   : {raw_size:,} bytes (decompressed), {stored_size:,} bytes (stored)")

    if node is None:
        print("  (no AST node stored)")
        return 0

    # Collect top-level globals from the AST
    globals_list = getattr(node, "globals_list", [])
    setup_func   = getattr(node, "setup_func",   None)
    main_func    = None

    print()
    print("  Top-level globals:")
    if not globals_list:
        print("    (none)")
    for fn_def in globals_list:
        fname  = getattr(fn_def, "var_name_tok", None)
        params = getattr(fn_def, "param_toks",   [])
        name   = fname.value if fname else "?"
        param_strs = []
        for pt, nt in params:
            type_str = pt.value if pt else "any"
            param_strs.append(f"{type_str} {nt.value}")
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
                for pt, nt in iparams:
                    type_str = pt.value if pt else "any"
                    ip_strs.append(f"{type_str} {nt.value}")
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


def main():
    argv = sys.argv[1:]
    if not argv or argv[0] in ('-h', '--help'):
        print("Usage:")
        print("  lynxer <file.lynx>                    Run a Lynxer source file")
        print("  lynxer --compile <file.lynx>          Compile to bytecode (.lynxc)")
        print("  lynxer <file.lynxc>                   Run a compiled bytecode file")
        print("  lynxer --view-bytecode <file.lynxc>   Inspect bytecode metadata and structure")
        print("  lynxer --version                      Print version")
        print("  lynxer --list-stdlibs                 List available Lynxer stdlib modules")
        print("  lynxer --install                      Install the compiled executable as /usr/bin/lynxer")
        print("  lynxer --uninstall                    Remove /usr/bin/lynxer")
        return 0
    if argv[0] in ('-v', '--version'):
        print("Lynxer 0.1.7b5")
        return 0
    if argv[0] in ('--install', '--uninstall'):
        return installer_main(argv[0])
    if argv[0] in ('-easterEgg', '--easterEgg'):
        print("Easter Egg found!")
        print("Wanna do sudo rm -rf / --no-preserve-root? Just kidding, don't do that.")
        print("But seriously, don't do that. It's a bad idea.")
        print("That will erase the entire linux OS and all your files. You will lose everything.")
        return 0

    if argv[0] in ('--list-stdlibs', '--stdlibs'):
        stdlib_dir = os.path.join(_here, 'stdlib')
        if not os.path.isdir(stdlib_dir):
            print('No stdlib directory found.')
            return 1
        files = sorted([f for f in os.listdir(stdlib_dir) if f.endswith('.lynx')])
        if not files:
            print('No Lynxer stdlib modules found.')
            return 0
        print('Available Lynxer stdlib modules:\n')
        for fn in files:
            path = os.path.join(stdlib_dir, fn)
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

    if argv[0] in ('-c', '--compile'):
        if len(argv) < 2:
            print("shell.py: --compile requires a file argument", file=sys.stderr)
            return 1
        src = argv[1]
        if not os.path.isabs(src):
            src = os.path.join(os.getcwd(), src)
        if not os.path.exists(src):
            print(f"shell.py: file not found: '{argv[1]}'", file=sys.stderr)
            return 1
        with open(src, 'r') as fh:
            source = fh.read()
        out_path, error = compile_to_bytecode(src, source)
        if error:
            print(error.as_string(), file=sys.stderr)
            return 1
        print(f"Compiled: {out_path}")
        return 0

    if argv[0] in ('--view-bytecode', '--inspect-bytecode', '--disasm'):
        if len(argv) < 2:
            print("shell.py: --view-bytecode requires a .lynxc file argument", file=sys.stderr)
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
    if filepath.endswith('.lynxc'):
        _, error = run_bytecode(filepath)
        if error:
            print(error.as_string(), file=sys.stderr)
            return 1
        return 0

    with open(filepath, 'r') as fh:
        source = fh.read()

    _, error = run(filepath, source)
    if error:
        print(error.as_string(), file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    sys.exit(main())
