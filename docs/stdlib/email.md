# email

Parse RFC 5322/MIME messages and construct multipart messages. Parsing returns
JSON with lower-case headers and a `parts` array; text bodies are strings and
binary parts are base64 objects, with content type and optional filename.
Construction accepts JSON with `from`, `to`, `subject`, `text`, optional `html`,
optional custom `headers`, and optional `attachments`. Each attachment has a
`filename`, optional `contentType`, and base64 encoded content. The result is a
raw MIME message. Inputs are limited to 16 MiB.

| Function | Signature | Returns |
|---|---|---|
| `emailParse` | `(str message)` | parsed headers and MIME parts as JSON |
| `emailBuild` | `(str json)` | RFC 5322 message with MIME headers |

Malformed messages or construction data return `""`.
