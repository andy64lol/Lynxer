"""Build Lynxer's bundled C++ memory extension."""

from pathlib import Path
import os

from setuptools import Extension, setup


ROOT = Path(__file__).resolve().parent
os.chdir(ROOT)

if os.name == "nt":
    compile_args = ["/std:c++17"]
    link_args = []
else:
    compile_args = ["-std=c++17", "-pthread"]
    link_args = ["-pthread"]

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
        )
    ],
)