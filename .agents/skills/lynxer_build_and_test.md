---
name: lynxer-build-and-test
description: Resource-aware build, lint, format, test, debug and benchmark workflow for the Lynxer repository on its Arch Linux x86_64 development host.
---

# Lynxer build and test workflow

Use this skill for any change in this repository. It records the supported
commands, the layers of the test suite, and the practical limits of this host.

## Host facts

- OS: Arch-based Nyarch Linux, rolling release, `x86_64`.
- RAM: about 7.6 GiB total. Use `make -j2` at most; `-j$(nproc)` can OOM the
  C++/Rust build. Use `-j1` when another heavy process is running.
- The checkout lives on removable media under `/run/media/...`. Check
  `df -h .` before a clean rebuild, `make dist`, or any large artifact.
- Available: `git`, `make`, `c++`/`gcc`, `clang`, `cargo`/`rustc`, `python3`,
  `node`, `npm`, `cmake`, `ninja`.
- Optional: `aarch64-linux-gnu-g++` for ARM64 cross-builds; `curl` plus
  `openssl` for the server TLS fixture; a display and audio device for GUI and
  sound fixtures.
- `cargo` is mandatory: `lynxerToolchain` fails immediately if it is missing.
  Do not install packages or toolchains without the user's approval.

## First commands

```bash
make buildLynxer          # interpreter, C++ stdlib modules, Rust backends, SDK
./lynxer/lynxer --version
make check                # full suite + lint of syntax.lynx
```

`make build` and `make buildAll` are aliases for `buildLynxer`.

## Build and test targets

| Target | What it does |
| --- | --- |
| `make buildLynxer` | Builds `lynxer/lynxer`, every `lynxer/stdlib/*.so`, and the embedding SDK. |
| `make testLynxer` | Full Lynxer suite: contract check, fixtures, parity, debugger, LSP, install, export ABI. |
| `make check` | `testLynxer` plus `--lint syntax.lynx`. |
| `make test` | Alias for `testLynxer`. |
| `make testLynxerGui` | Real-window GUI tests; requires `DISPLAY`. |
| `make testLynxerInstall` | Installs into a throwaway prefix, no root. |
| `make testLynxerAmd64Syscalls` | AMD64-only syscall fixture; run on `x86_64`. |
| `make testLynxerArm64Syscalls` | ARM64-only syscall fixture; requires an ARM64 host. |
| `make benchLynxer` | Interpreter/optimizer/compiled startup benchmark. |
| `make dist` | Relocatable bundle under `dist/lynxer-<version>-<os>-<arch>.tar.gz`. |
| `make clean` | Removes transient files only. |
| `make cleanLynxer` | Removes the interpreter, objects, modules, SDK, and Rust targets. Destructive: ask first. |
| `make cleanAll` | Both clean targets. Destructive: ask first. |

## Fast iteration

```bash
./lynxer/lynxer program.lynx
./lynxer/lynxer --no-opt program.lynx
./lynxer/lynxer --lint program.lynx
./lynxer/lynxer --ast program.lynx
./lynxer/lynxer --format program.lynx          # rewrites in place
./lynxer/lynxer --compile program.lynx -o /tmp/opencode/program
./lynxer/lynxer --list-stdlibs
python3 lynxer/scripts/check_module_contracts.py --verbose
python3 lynxer/scripts/test_debugger.py
python3 lynxer/scripts/test_lsp.py
```

`--format` and `--format-oneline` rewrite the input file. Do not run them on a
file with uncommitted work without checking the diff first.

## What the suite covers

1. `lynxer/scripts/check_module_contracts.py` compares every stdlib wrapper with
   its backend and fails on op-coverage, packed-argument, or unused-registration
   defects.
2. Inline expected outputs in the root `Makefile` for core fixtures such as
   `builtins.lynx`, `conditions.lynx`, and the milestone fixtures.
3. `lynxer/examples/*.lynx` with sibling `.expected` files, diffed on stdout and
   stderr.
4. Direct-versus-compiled parity for `LYNXER_PARITY_FIXTURES`, so a change must
   survive `--compile` as well as the interpreter.
5. Debugger and language-server tests.
6. `--install` layout and `--emit-library` C ABI checks.
7. Conditional GUI, audio, and TLS fixtures. CI sets `LYNXER_SKIP_DISPLAY=1`
   when no display is available.

## Common failures

- **`cargo is required`**: a Rust toolchain is missing from `PATH`. Report it
  and ask before installing anything.
- **Source-located `lynxer:` error**: read the exact `file:line:column` and the
  caret line; the diagnostic usually names the real problem.
- **Fixture mismatch**: run the fixture directly, compare the actual output with
  the expected file or inline `Makefile` string, and update the expectation only
  when the new behavior is intentional.
- **GUI/audio/TLS skipped**: that is a host limitation, not a pass. State what
  was skipped and why.
- **Stale binary after a header change**: run `make buildLynxer`. The Makefile
  tracks headers, but a manually copied `.so` or binary may be stale.
- **Disk full on the removable checkout**: stop and report; do not delete
  caches, build trees, or user files without approval.

## Git safety

- Never push unless the user explicitly asks.
- Do not commit unless the user asks or the task clearly includes it.
- Leave unrelated working-tree changes alone, including generated skill files
  and build artifacts.
- Before reporting completion, run `git diff --check` on the files you changed.

## Definition of done

- The code builds with `make buildLynxer`.
- The narrowest relevant fixture or command passes.
- `make check` passes for cross-cutting changes.
- Documentation, `syntax.lynx`, and expected outputs are updated when behavior
  changes.
- The final report lists exactly which checks ran and which were skipped.
