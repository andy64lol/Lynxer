# pdf

Generate simple text PDFs, extract text, and inspect page count/version. PDF
bytes are transported as base64 strings. Generation uses a built-in Helvetica
font and places up to 50 lines on one A4 page. Existing PDFs are parsed with
lopdf; inputs are bounded before decoding and parsing.

| Function | Signature | Returns |
|---|---|---|
| `pdfGenerate` | `(str text)` | base64 encoded PDF |
| `pdfText` | `(str base64Pdf)` | extracted page text |
| `pdfInfo` | `(str base64Pdf)` | JSON object containing `pages` and `version` |

Invalid input returns `""`.
