from setuptools import Extension, setup

setup(
    name="lynxer-experimental-c",
    version="0.1.0",
    ext_modules=[
        Extension(
            "c",
            sources=["c.cpp"],
            language="c++",
        )
    ],
)
