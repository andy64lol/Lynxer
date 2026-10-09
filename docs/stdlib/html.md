# html

Parse HTML with the HTML5 parser and query it with CSS selectors. Documents and
selected elements cross the native ABI as JSON. Input is limited to 16 MiB.
Malformed selector syntax returns `""`; a selector that matches nothing returns
`[]`. `htmlParse` returns a nested tree of `{tag, attributes, text, children}`.

| Function | Signature | Returns |
|---|---|---|
| `htmlParse` | `(str source)` | document element tree as JSON |
| `htmlSelect` | `(str source, str selector)` | matching elements with tag, attributes, text and inner HTML |
| `htmlText` | `(str source, str selector)` | text of the first match |
| `htmlAttr` | `(str source, str selector, str name)` | attribute value of the first match |
