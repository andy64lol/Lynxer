# !/usr/bin/env python3
"""Development shim — run from."""
import sys
import os
import re

_here   = os.path.dirname(os.path.abspath(__file__))   # .../lynxer/
_parent = os.path.dirname(_here)                         # repo root
if _parent not in sys.path:
    sys.path.insert(0, _parent)

from lynxer import run, compile_to_bytecode, run_bytecode  # noqa: E402

def main():
    argv = sys.argv[1:]
    if not argv or argv[0] in ('-h', '--help'):
        print("Usage:")
        print("  lynxer <file.lynx>              Run a Lynxer source file")
        print("  lynxer --compile <file.lynx>    Compile to bytecode (.lynxc)")
        print("  lynxer <file.lynxc>             Run a compiled bytecode file")
        print("  lynxer --version                Print version")
        print("  lynxer --list-stdlibs           List available Lynxer stdlib modules")
        return 0
    if argv[0] in ('-v', '--version'):
        print("Lynxer 0.1.7b3")
        return 0
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
        IMPORT_RE = re.compile(r"^\s*(?:from\s+([\w\.]+)\s+import|import\s+([\w\.]+))")
        files = sorted([f for f in os.listdir(stdlib_dir) if f.endswith('.lynx')])
        if not files:
            print('No Lynxer stdlib modules found.')
            return 0
        print('Available Lynxer stdlib modules:')
        for fn in files:
            path = os.path.join(stdlib_dir, fn)
            imports = set()
            try:
                with open(path, 'r', encoding='utf-8') as fh:
                    for line in fh:
                        m = IMPORT_RE.match(line)
                        if m:
                            mod = m.group(1) or m.group(2)
                            if mod:
                                imports.add(mod.split('.')[0])
            except Exception:
                pass
            name = os.path.splitext(fn)[0]
            if imports:
                print(f"  - {name}: imports {', '.join(sorted(imports))}")
            else:
                print(f"  - {name}")
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
