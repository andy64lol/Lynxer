"""Build Lynxer's bundled C++ memory extension."""

from pathlib import Path
import os

from setuptools import Extension, setup


ROOT = Path(__file__).resolve().parent
os.chdir(ROOT)

setup(
    name="lynxer-cpp",
    version="0.2.0",
    ext_modules=[
        Extension(
            "cpp",
            sources=[str(ROOT / "cpp.cpp")],
            language="c++",
        )
    ],
)