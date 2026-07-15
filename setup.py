"""
setup.py — allows `pip install .` or `pip install -e .` for development.
After installation, `lynxer` is available on the command line.
"""
from setuptools import setup, find_packages

setup(
    name='lynxer',
    version='0.1.0',
    description='A statically-flavoured, C-style scripting language written in Python',
    python_requires='>=3.8',
    install_requires=[
        'cython',   # required for the rawPyx() built-in / rawPyx(){ } block
        'setuptools',  # provides the distutils shim Cython.Build.Inline needs on 3.12+
    ],
    packages=find_packages(),
    include_package_data=True,
    package_data={
        'lynxer': [
            'stdlib/*.lynx',
            'examples/*.lynx',
        ],
    },
    entry_points={
        'console_scripts': [
            'lynxer = lynxer.__main__:main',
        ],
    },
    classifiers=[
        'Programming Language :: Python :: 3',
        'License :: OSI Approved :: MIT License',
        'Operating System :: OS Independent',
    ],
)
