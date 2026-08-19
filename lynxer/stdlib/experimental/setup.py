"""Build the optional experimental C++ extension.

Run from the repository root:
    python lynxer/stdlib/experimental/setup.py build_ext --inplace
"""

from pathlib import Path
import os

from setuptools import Extension, setup


ROOT = Path(__file__).resolve().parent
# setuptools' --inplace copies the extension into the process working
# directory. Always build from this directory so c*.so/c*.pyd stays beside
# c.cpp and lowLevel.lynx, even when invoked from the repository root.
os.chdir(ROOT)

setup(
    name="lynxer-experimental-c",
    version="0.1.0",
    ext_modules=[
        Extension(
            "c",
            sources=[str(ROOT / "c.cpp")],
            language="c++",
        )
    ],
)
