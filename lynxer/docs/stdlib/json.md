# json

JSON encoding, decoding, querying and mutation.

**Backend:** native — `stdlib/json.so`, built from the Rust crate `rust/json`
over [`serde_json`](https://docs.rs/serde_json) with the `preserve_order`
feature so object keys keep their insertion order. Output spacing and the
pretty-print indents match the previous nlohmann-based backend byte for byte.
**Import:** `import("json")` → `global.json.*`

Objects preserve key insertion order, and non-finite numbers are written as
`null` so output is always valid JSON.

| Function | Signature | Returns |
| --- | --- | --- |
| `jsonValid` | `(str s)` | `true` if `s` parses as JSON |
| `jsonParse` | `(str s)` | `s` re-indented with 2 spaces, or `""` on error |
| `jsonPretty` | `(str s)` | `s` re-indented with 4 spaces, or `""` on error |
| `jsonGet` | `(str s, str key)` | Value at `key` rendered as text, or `""` |
| `jsonGetInt` | `(str s, str key)` | Integer at `key`, or `0` |
| `jsonGetFloat` | `(str s, str key)` | Float at `key`, or `0.0` |
| `jsonGetBool` | `(str s, str key)` | Truthiness of the value at `key` |
| `jsonKeys` | `(str s)` | Top-level keys, comma-separated |
| `jsonStringify` | `(str s)` | `s` quoted and escaped as a JSON string |
| `jsonArray` | `(list lst)` | JSON array built from a Clynxer list |
| `jsonObject` | `(list lst)` | JSON object from a flat key, value, key, value list |
| `jsonHas` | `(str s, str key)` | `true` if the object has `key` (or the array contains it) |
| `jsonLength` | `(str s)` | Key, element or character count; `0` on error |
| `jsonSet` | `(str s, str key, str val)` | Updated JSON; `val` is parsed as JSON when possible |
| `jsonSetInt` | `(str s, str key, int value)` | Updated JSON with an integer value |
| `jsonDelete` | `(str s, str key)` | Updated JSON without `key` |
| `jsonMerge` | `(str a, str b)` | Shallow merge; `b` wins |
| `jsonType` | `(str s, str key)` | `string`, `int`, `float`, `bool`, `null`, `object`, `array` or `unknown` |
| `jsonBuild` | `(str pairs)` | Object from `"k=v\|k=v"` (values are strings) |

`jsonGet` renders arrays and objects as compact JSON text, and booleans as
`true`/`false`.

## Example

```lynx
global setup(){ import("json"); }

global main(){
    str doc = "{\"name\": \"Ada\", \"age\": 36}";
    println(global.json.jsonGet(doc, "name"));
    println(global.json.jsonSetInt(doc, "age", 37));
    println(global.json.jsonParse("[1,2]"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [parity.md](../parity.md) — the parity scope with Python Clynxer.
- [limitations.md](../limitations.md) — the full divergence register.
