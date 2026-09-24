# colorlib

Terminal color and text-style helpers using ANSI escape sequences.

Usage examples:

- `colorlib.red("error")` → returns the input string wrapped in red ANSI codes.
- `colorlib.bold("text")` → returns the input string in bold.

Provided functions (high-level):

- `black(text)`, `red(text)`, `green(text)`, `yellow(text)`, `blue(text)`, `magenta(text)`, `cyan(text)`, `white(text)` — foreground colors.
- `brightBlack(text)`, `brightRed(text)`, ... `brightWhite(text)` — bright foreground colors.
- `bgBlack(text)`, `bgRed(text)`, ... `bgWhite(text)` — background colors.
- `bold(text)`, `dim(text)`, `italic(text)`, `underline(text)`, `blink(text)`, `inverse(text)`, `strike(text)` — text styles.
- `error(text)`, `success(text)`, `warn(text)`, `info(text)`, `heading(text)` — preconfigured styled helpers.

Low-level helpers:

- `reset()` — reset ANSI attributes.
- `ansi(text, code)` — wrap `text` in the ANSI `code`.
- `clearScreen()` — clear the terminal screen.
- `cursorHome()` — move cursor to home position.