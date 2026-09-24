# Parity scope

Clynxer is a standalone C++ implementation of Lynxer, not a drop-in clone of the
Python implementation. The Python implementation is the behaviour reference, but
Clynxer has deliberately diverged in several places, so "does it match Python?"
is only a meaningful question for part of the surface. This page draws that
line: what is expected to match, what is expected to differ, and what has no
Python counterpart at all.

The canonical, itemised register of differences is
[limitations.md](limitations.md). This page is the short version used to decide
what a test may compare.

## Machinery

`make testCLynxer` is the gate. It runs, in order:

1. `clynxer/scripts/check_module_contracts.py` — every stdlib wrapper against
   its native backend;
2. `clynxer/scripts/check_golden.py` — the CLI surface and source-located
   diagnostic text (`clynxer/golden/cases.json`);
3. the optimizer fixture, diffed against `--no-opt` byte-for-byte, with its
   transformation counts asserted from `CLYNXER_OPT_REPORT`;
4. the deprecated-operator fixture, whose warning must still reach stderr;
5. every `examples/*.expected` fixture, diffed byte-for-byte on stdout+stderr —
   including the lexical-divergence, low-level native-memory, and syscall
   fixtures;
6. interpreted-versus-`--compile` parity for a set of fixtures;
7. the `--list-stdlibs` module coverage, native-signature module, and
   bundled-executable / `--include` / bytecode-removal checks.

Both Clynxer CI workflows run it as
`make testCLynxer CLYNXER_SKIP_DISPLAY=1`, which drops the fixtures that need a
display or an audio device (`stdlib_game`, `stdlib_sound`, `stdlibTestAll`,
`game_clicker`). `CLYNXER_GAME_HEADLESS=1` is the separate run-time switch that
lets the `game` module run without a window on a host that has a display.

All of these pin **Clynxer's own** output. Nothing in CI runs the Python
implementation, so a Clynxer fixture records Clynxer's behaviour by design — a
fixture that asserts wrong behaviour will pass. That is why the wrapper/backend
contract check and the golden CLI cases exist alongside the fixtures.

## Where parity is intended

- The **language core**: literals, control flow, the value model, functions,
  classes/structs/enums, pattern matching, operators, errors. A program that
  uses only the shared surface should behave the same in both implementations,
  and Clynxer's `.expected` fixtures are written against that behaviour.
- The **stdlib APIs whose documentation says so**: `sqldb` (byte-identical
  SQL/JSON output), `json` key order and separators, `csv` line terminators,
  `math` statistics results, and the `filesystem`/`process`/`networking`/
  `sound` built-in families all aim to match the reference for their successful
  and error paths.

Parity is checked by Clynxer's own fixtures, not by running Python. The one
cross-implementation measurement is the **baseline comparison**: on 2026-09-13,
15 of 55 Python test fixtures passed. The failures were mostly surface that
Clynxer implements itself rather than missing features, and the number is
expected to change as both implementations evolve. Treat it as history, not a
gate.

## Deliberately divergent (denylist)

These are **not** parity targets. A test must assert Clynxer's own behaviour for
them; comparing against Python for these would be a false failure.

| Area | Divergence | See |
|------|-----------|-----|
| Lexer | no bare `!`; no `/* */`; no `\x`/`\u` escapes; only `//`, `///` and `////` comments | [limitations.md](limitations.md#language-and-toolchain) |
| Compiler | no bytecode or `.lynxc`; `--compile` writes an ELF embedding source | [limitations.md](limitations.md#language-and-toolchain) |
| Modules | `global.name(...)` resolves to a builtin, not the module's own function | [limitations.md](limitations.md#language-and-toolchain) |
| `sound` | backend failure text differs; `pause`/`resume`/`stop` work | [limitations.md](limitations.md#sound) |
| `nativeThread*` | cooperative, one interpreter lock; `returnType` is `codeblock` | [limitations.md](limitations.md#nativethread--implemented-on-a-cooperative-model) |
| `image` | pixel/`info` formatting; `grayscale` keeps alpha | [limitations.md](limitations.md#image) |
| `lua` | `luaExists` is a bool; vendored engine; error-text shape | [limitations.md](limitations.md#lua) |
| `re` / `regex` | `std::regex` ECMAScript grammar, sentinel results | [limitations.md](limitations.md#re-and-regex) |
| `json` | non-finite numbers encode as `null` | [limitations.md](limitations.md#json) |
| `csv` | `\r\n` terminators and JSON-scalar rendering | [limitations.md](limitations.md#csv) |
| `os` / `path` / `sys` | no Python runtime or `sys.path` family | [limitations.md](limitations.md#sys) |
| `cli` | Click/Typer builders not defined | [limitations.md](limitations.md#cli) |
| `multiprocessing` | worker threads, not processes | [limitations.md](limitations.md#multiprocessing) |
| `js` | requires `node`; no timeout | [limitations.md](limitations.md#js) |
| `debug` | `dump`/`pp` use `strOf`, not `repr` | [limitations.md](limitations.md#debug) |
| `--ast` | prints Clynxer's own node/field names over the executable AST | [limitations.md](limitations.md#cli-tools) |
| `math` | statistics reimplemented without NumPy | [limitations.md](limitations.md#math) |
| `tui` | placeholder backend, not Rich | [limitations.md](limitations.md#tui) |
| `sqldb` | failures returned in band | [limitations.md](limitations.md#sqldb) |
| Not ported | `tkinter`, `tkinterPlus`, `turtle`, `venv`, `rawPy*` | [limitations.md](limitations.md#modules-that-are-not-ported) |

## No Python counterpart (out of scope for parity)

These exist only in Clynxer, so there is nothing to compare them to; they are
gated by Clynxer's own tests and the golden CLI cases.

- the `--compile` ELF executable and `--bundle` alias;
- `--include` and the `bundledFile()` / `bundledFiles()` builtins;
- the Rust `cdylib` native-module ABI and its `lynxer_module_init_v1` /
  `lynxer_module_attach_v1` entry points;
- the `network` + `server` modules (the Python side has the older `http`/`net`);
- cooperative `nativeThread*` execution;
- the AST optimizer, `--no-opt` and `CLYNXER_OPT_REPORT`;
- the `--format`/`--format-oneline` formatter (it preserves `//` and
  `///`/`////` comments verbatim and is idempotent), the
  `--validate-executeable` self-check, and `--install`/`--uninstall`;
