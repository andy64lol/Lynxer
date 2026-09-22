#!/usr/bin/env python3
"""Select .lynx files that only import Python stdlib modules.

Usage: python scripts/select_pure_stdlib.py <src_dir> <dst_dir>
Copies safe .lynx files from src_dir to dst_dir.
"""
import os
import re
import sys
import shutil
import importlib.util


IMPORT_RE = re.compile(r"^\s*(?:from\s+([\w\.]+)\s+import|import\s+([\w\.]+))")


def find_imports(path):
    mods = set()
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            m = IMPORT_RE.match(line)
            if m:
                mod = m.group(1) or m.group(2)
                if mod:
                    mods.add(mod.split('.')[0])
    return mods


def is_stdlib_module(name, local_names, stdlib_names=None):
    if name in local_names:
        return True
    try:
        if stdlib_names is not None:
            return name in stdlib_names
        if hasattr(sys, 'stdlib_module_names'):
            return name in sys.stdlib_module_names
        spec = importlib.util.find_spec(name)
        if spec is None:
            return False
        origin = getattr(spec, 'origin', '') or ''
        if not origin:
            return False
        origin = os.path.normcase(origin)
        if 'site-packages' in origin or 'dist-packages' in origin:
            return False
        return True
    except Exception:
        return False


def main():
    if len(sys.argv) != 3:
        print('Usage: select_pure_stdlib.py <src_dir> <dst_dir>')
        return 2
    src = sys.argv[1]
    dst = sys.argv[2]
    os.makedirs(dst, exist_ok=True)

    files = [f for f in os.listdir(src) if f.endswith('.lynx')]
    local_names = {os.path.splitext(f)[0] for f in files}

    stdlib_names = None
    if hasattr(sys, 'stdlib_module_names'):
        stdlib_names = set(sys.stdlib_module_names)

    kept = []
    for fn in files:
        path = os.path.join(src, fn)
        mods = find_imports(path)
        ok = True
        for m in mods:
            if not is_stdlib_module(m, local_names, stdlib_names):
                ok = False
                break
        if ok:
            shutil.copy2(path, os.path.join(dst, fn))
            kept.append(fn)

    print(f'Kept {len(kept)}/{len(files)} files:')
    for k in kept:
        print('  -', k)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
