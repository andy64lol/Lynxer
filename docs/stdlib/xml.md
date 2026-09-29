# xml

XML parsing and serialization.

**Backend:** native — `stdlib/xml.so`, built from the Rust crate `rust/xml`
over `quick-xml`.

**Import:** `import("xml")` → `global.xml.*`

## How documents travel

An XML document maps onto a **JSON element tree**, the shared structured-value
bridge (see [native-module-abi.md](../native-module-abi.md)):

```json
{"name": "root", "attributes": {"a": "1"}, "text": "hi", "children": [ ... ]}
```

`text` is the character data directly inside the element; each child is another
element. Comments, processing instructions and the doctype are ignored, and **no
external entity is ever resolved**.

A failure is a scalar sentinel: a malformed document yields `""` and `xmlValid`
yields `false`; `xmlUnescape` yields `""` for a malformed entity.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `xmlParse` | `(str text)` | the element tree as JSON, or `""` |
| `xmlSerialize` | `(str json)` | the XML document, or `""` |
| `xmlValid` | `(str text)` | `true` / `false` |
| `xmlEscape` | `(str text)` | the text with `&`, `<`, `>`, `"`, `'` escaped |
| `xmlUnescape` | `(str text)` | the text with entities decoded, or `""` |

## Example

```lynx
global setup(){ import("xml"); }

global main(){
    str text = "<a x=\"1\">hi<b/></a>";

    println(global.xml.xmlParse(text));
    println(global.xml.xmlValid(text));
    println(global.xml.xmlEscape("a<b&c"));

    // Parse then serialize is a round trip.
    println(global.xml.xmlSerialize(global.xml.xmlParse(text)));
}
```

---

## See also

- [Standard library contracts](../stdlib-contracts.md)
- [Built-in functions](../builtins.md)
- [Limitations](../limitations.md)
