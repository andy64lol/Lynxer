#!/usr/bin/env python3
"""Entry point: delegates to the Lynxer CLI in lynxer/shell.py."""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from lynxer.shell import main

if __name__ == "__main__":
    sys.exit(main())
