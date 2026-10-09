# markdown

Render Markdown as HTML with pulldown-cmark. The optional `extensions` integer
uses bit 0 to enable tables, footnotes, strikethrough and task lists (enabled by
default). Raw HTML is preserved by the renderer.

| Function | Signature | Returns |
|---|---|---|
| `markdownRender` | `(str source, int extensions = 1)` | rendered HTML |
| `markdownParse` | `(str source, int extensions = 1)` | alias of `markdownRender` |

Input is limited to 16 MiB; failures return `""`.
