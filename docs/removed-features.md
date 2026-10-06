# Removed features

Features the original `docs-legacy/` reference documented that the standalone
runtime does not implement. Each entry records the current behaviour and the
replacement.

## Bytecode (`.lynxc`)

The bytecode backend is gone. `--compile` (`-c`) produces a standalone
executable instead, and there is no separate cache format.

| Input | Behaviour |
| --- | --- |
| `lynxer file.lynxc` | `lynxer: bytecode files are no longer supported; compile the .lynx source with --compile instead` |
| `--view-bytecode`, `--inspect-bytecode`, `--disasm` | `lynxer: '<flag>' was removed with the bytecode backend; use lynxer --compile instead` |
| `--benchmark-compile`, `--no-cache` | Same "removed with the bytecode backend" error |

See [CLI.md](CLI.md) for the current flags.

## Python bridging (`rawPy`, `rawPyx`, `embedPy`)

Lynxer does not embed or link a Python runtime, and these are **removed fully**
— not a planned reopen.

| Name | Behaviour |
| --- | --- |
| `rawPy` / `rawPyx` | `<name>() is not supported in Lynxer yet` |
| `cleanRawPyxCache` | Same as above |
| `embedPy` | `Python bridging (embedPy) is not supported in Lynxer` |

`rawPy { ... }` is not special syntax: it parses as an ordinary call with an
inline codeblock and then fails as above. `embedPy.x(...)` is rejected at the
member-call level. See [builtins.md](builtins.md).

## Python introspection getters

`os.getPythonVersion()`, `os.getPythonImplementation()` and the `python`,
`pythonImplementation` and `pythonExecutable` fields of `os.getSystemInfo()`
are **removed**. They existed only to mimic a Python host — `""` and `"Lynxer"`
are not values any program should branch on — and there is no Python runtime to
report. The fields are simply absent from `getSystemInfo()` now; the `sys`
introspection surface (`sys.path`, `addPath`, `getModules`, …) was never
implemented and stays unplanned.

## `http` and `net` modules

The legacy `http` and `net` modules are **not provided**, and no compatibility
shim is planned. `network` is the client — HTTP, WebSocket, and URL handling in
one module — and `server` covers serving, so a program has one API surface
rather than three overlapping ones. The legacy names (`httpGet`, `netSocket`, …)
are not aliases; port a program to `global.network.*` / `global.server.*`. See
[stdlib/network.md](stdlib/network.md).

## `venv`

A virtual-environment manager is a Python concept with no equivalent in a
standalone runtime; the module is not provided.

## `tkinter` / `turtle`

There is no Python GUI toolkit binding. The [graphics](stdlib/graphics.md)
module provides a Rust-backed (`macroquad`) immediate-mode drawing, window,
input and UI toolkit instead; it is not a `tkinter` clone. `turtle`'s Rust
crate has been unmaintained since 2019.

## Python → C++ migration

There is no longer a Python implementation to migrate from. The former
`clynxer/` tree, the `lynxer.py` facade, the `lynxer-py` reference build and the
v9 bytecode contract are all gone; `lynxer/` (C++17) is the only implementation.

| Removed | Replacement |
| --- | --- |
| Python implementation and facade | `lynxer/` (C++), built with `make buildLynxer` |
| Python-only build paths (`clynxer/`, `make buildCpp`) | The root `Makefile` |
| Bytecode migration/version contract | `--compile` to a standalone ELF executable |

See [legacy-surface.md](legacy-surface.md) for the full list of original features
and their replacements.

## Constraints outside the implementation plan

These behaviors are intentional boundaries or depend on unavailable hardware
or unsupported host platforms. They are documented for users, but are not
approved work items:

- **Windows and non-POSIX native loading.** Lynxer targets Linux/POSIX; native
  module loading uses `.so` libraries and watch descriptors. Windows DLL
  loading and a Windows watch backend are not planned.
- **Hardware-only graphics and audio.** Headless mode cannot execute GPU
  shaders, materials or 3D meshes, or play sound without an audio device.
  `macroquad` audio, native OS widgets and gamepad support are not included.
- **Security bounds and path confinement.** Compression, archive extraction
  and parser input limits, plus rejection of archive/static paths that escape
  their destination, are deliberate protections and will not be removed.
- **Bounded payloads and sentinel errors.** The 64 MiB decompression/archive
  limits, parser input caps, 1 MiB random-payload cap, and existing in-band
  failure sentinels are safety and ABI contracts, not missing work.
- **Headless hardware features.** Headless graphics does not emulate GPU
  shaders, materials or 3D meshes; game animation and audio playback require
  their respective rendering/audio resources. The windowed renderer and
  device-backed playback cannot be made available on a machine without those
  devices.
- **Single-live native results.** The ABI's thread-local string result and
  single live byte result are retained: bundled modules do not need multiple
  simultaneous string results, and variable-sized data uses bytes or handles.
- **Cooperative interpreter execution.** Worker callbacks share mutable
  interpreter state, so a global lock serializes Lynxer evaluation.
  `nativeThread*` does not provide CPU-parallel evaluation, `async` local
  blocks are eager, and `await` joins a thread-backed task rather than
  suspending a coroutine.
- **Compatibility decisions.** `mathPlus` remains merged into `math`;
  `http`/`net` remain replaced by `network`/`server`; UUID v1/v6, RSA/ECDSA,
  the legacy Python-facing probes, and bytecode remain unavailable.
- **Stable API conventions.** Existing in-band failure sentinels, ASCII-only
  helper behavior, JSON document bridges, text-oriented UUID representation,
  TTY fallbacks, and established module-specific return formats remain
  compatibility contracts unless a separately approved, versioned change is
  made.
