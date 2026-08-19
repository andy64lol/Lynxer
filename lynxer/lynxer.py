from __future__ import annotations

import os
import itertools
import string
import sys
import textwrap
import warnings
from typing import Any, ClassVar

try:
    from strings_with_arrows import string_with_arrows
except ImportError:
    from lynxer.strings_with_arrows import string_with_arrows  # type: ignore[no-redef]

DIGITS = "0123456789"
_SOURCE_DIR = os.path.dirname(os.path.abspath(__file__))
STDLIB_DIR = os.path.join(_SOURCE_DIR, "stdlib")
LETTERS = string.ascii_letters
LETTERS_DIGITS = LETTERS + DIGITS

# NOTE: This update intentionally preserves the existing runtime implementation.
# The experimental stdlib resolution change is applied in _module_path below.
