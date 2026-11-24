# Standard library

Wynxer ships a web-focused standard library. The backends are implemented
in Rust and exposed to Wynxer as built-in functions.

## Web APIs (planned)

| Module | What it covers |
| --- | --- |
| `dom` | Query, create and update elements |
| `fetch` | HTTP client |
| `storage` | `localStorage` / `sessionStorage` |
| `router` | Client-side routing |
| `forms` | Form handling and validation |
| `anim` | Animation and transitions |

## Built-ins

| Function | Signature | Meaning |
| --- | --- | --- |
| `alert` | `(str)` | show a browser alert |
| `println` | `(...)` | print to the console |
| `strOf` | `(any) -> str` | convert a value to a string |

## See also

- [language.md](language.md) — the language
- [wpf.md](wpf.md) — the page format
- [elements.md](elements.md) — the element API
- [CLI.md](CLI.md) — the command line
