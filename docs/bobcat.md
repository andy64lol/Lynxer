# Bobcat

Bobcat is a separate IDE project extracted from Lynxer's former editors. It
contains two independent frontends; Lynxer's `--cli-ide`/`--gui-ide` and
`lynxer/ide/` have been removed, so Bobcat is now the only IDE.

| Component | Directory | What it is |
| --- | --- | --- |
| **Bobcat CLI** | `bobcat/cli/` | Standalone full-screen terminal IDE (ratatui) for `.lynx`, `.wynx`, `.wpf` |
| **Bobcat IDE** | `bobcat/ide/` | Native GUI IDE (iced) — rebranded copy of `lynxer/ide` |

Both IDEs enter a **specialised Wynxer mode** when launched in a project
with `.wynxcfg` and a `.wpf` page: the entry page opens automatically and the
toolbar becomes Build & Serve / Check / Format for the whole project.

Both IDEs are clients of the language engines: they detect the file type and
drive `lynxer` for `.lynx` and `wynxer` for `.wynx`/`.wpf` (highlighting,
completions, build/lint/format). Neither embeds a compiler.

## Quick start

From the repository root:

```bash
make buildBobcatCli   # release binary at bobcat/cli/target/release/bobcat-cli
make buildBobcatIde   # release binary at bobcat/ide/target/release/bobcat-ide (slow LTO)
make buildBobcat      # both IDEs
make testBobcat       # both test suites
```

Or build directly:

```bash
# CLI IDE — no display needed
cd bobcat/cli
cargo build --release
./target/release/bobcat-cli path/to/file.lynx

# GUI IDE — needs X11/Wayland + a GPU (or software renderer)
cd ../ide
cargo build --release
./target/release/bobcat-ide
```

## Where to go next

| Page | Contents |
| --- | --- |
| [bobcat-cli.md](bobcat-cli.md) | Keys, engine detection, build and run |
| [bobcat-ide.md](bobcat-ide.md) | Native GUI features and runtime dependency |

## CI

Four workflows under `.github/workflows/` build and check both IDEs on Linux
AMD64/ARM64 and Windows AMD64/ARM64 (`build-bobcat-<platform>-<arch>.yml`).
They upload both binaries (`bobcat-cli` and `bobcat-ide`) as artifacts. The GUI
IDE's LTO link and test-profile compile give those jobs a 120-minute timeout.

## Relationship to Lynxer

Lynxer's built-in IDEs (`--cli-ide`, `--gui-ide`, and the `lynxer/ide/` crate)
were removed after Bobcat was extracted:

- `bobcat/cli/` reimplements the former terminal IDE as a standalone binary,
  and keeps a verbatim reference copy of the C++ code under `extracted/`.
- `bobcat/ide/` is a rebranded copy of the former native IDE (package
  `bobcat-ide`). It is now the only GUI IDE for the toolchain.
