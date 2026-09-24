# colorlib

ANSI colour and text-style helpers for terminal output.

**Backend:** pure — `stdlib/colorlib.lynx` builds ANSI escape sequences with the
core string builtins. **Import:** `import("colorlib")` → `global.colorlib.*`

Every styling function takes a string and returns that string wrapped in the
corresponding SGR sequence, terminated with a reset (`\e[0m`).

## Foreground colours — `(str text) -> str`

`black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white` (codes
30–37) and `brightBlack`, `brightRed`, `brightGreen`, `brightYellow`,
`brightBlue`, `brightMagenta`, `brightCyan`, `brightWhite` (codes 90–97).

## Background colours — `(str text) -> str`

`bgBlack`, `bgRed`, `bgGreen`, `bgYellow`, `bgBlue`, `bgMagenta`, `bgCyan`,
`bgWhite` (codes 40–47) and `bgBrightBlack`, `bgBrightRed`, `bgBrightGreen`,
`bgBrightYellow`, `bgBrightBlue`, `bgBrightMagenta`, `bgBrightCyan`,
`bgBrightWhite` (codes 100–107).

## Text styles — `(str text) -> str`

`bold` (1), `dim` (2), `italic` (3), `underline` (4), `blink` (5), `inverse`
(7), `strike` (9).

## Semantic helpers — `(str text) -> str`

`error` (bold red), `success` (bold green), `warn` (bold yellow), `info`
(bold cyan), `heading` (bold white).

## Low level

| Function | Signature | Notes |
| --- | --- | --- |
| `reset` | `() -> str` | Reset sequence `\e[0m` |
| `ansi` | `(str text, str code) -> str` | Wraps text in an arbitrary SGR code |
| `clearScreen` | `() -> str` | Clear-screen sequence |
| `cursorHome` | `() -> str` | Move the cursor to the top-left |

## Example

```lynx
global setup(){ import("colorlib"); }

global main(){
    println(global.colorlib.red("failed"));
    println(global.colorlib.heading("report"));
    println(global.colorlib.ansi("custom", "1;34"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
