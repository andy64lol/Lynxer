"""Build Lynxer's bundled native extensions."""

import os
from pathlib import Path

from setuptools import Extension, setup

ROOT = Path(__file__).resolve().parent
os.chdir(ROOT)

if os.name == "nt":
    compile_args = ["/std:c++17"]
    link_args = []
    bytecode_compile_args = ["/O2"]
else:
    compile_args = ["-std=c++17", "-pthread"]
    link_args = ["-pthread"]
    bytecode_compile_args = ["-O3"]

setup(
    name="lynxer-cpp",
    version="0.2.0",
    ext_modules=[
        Extension(
            "cpp",
            sources=[str(ROOT / "cpp.cpp")],
            language="c++",
            extra_compile_args=compile_args,
            extra_link_args=link_args,
        ),
        Extension(
            "bytecode_vm",
            sources=[str(ROOT / "bytecode_vm.c")],
            language="c",
            extra_compile_args=bytecode_compile_args,
        ),
    ],
)
