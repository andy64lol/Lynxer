# toml

TOML parsing and serialization.

**Backend:** native — `stdlib/toml.so`, built from the Rust crate `rust/toml`
over `toml`.

**Import:** `import("toml")` → `global.toml.*`

## How documents travel

A TOML document crosses the module ABI as a **JSON string**, the shared
structured-value bridge (see [native-module-abi.md](../native-module-abi.md)).
Pass the JSON to `jsonParse` or `jsonGet` to read it as Lynxer values, and build
a JSON object to serialize back to TOML. A TOML datetime becomes a JSON string.

A failure is a scalar sentinel: a document operation yields `""` and
`tomlValid` yields `false`. A JSON value TOML cannot hold (a top-level non-table,
or a `null`) makes `tomlSerialize` answer `""`.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `tomlParse` | `(str text)` | the document as JSON, or `""` |
| `tomlSerialize` | `(str json)` | the TOML document, or `""` |
| `tomlValid` | `(str text)` | `true` / `false` |
| `tomlGet` | `(str text, str key)` | the value at a dotted key as JSON, or `""` |

## Example

```lynx
global setup(){ import("toml"); import("json"); }

global main(){
    str text = "answer = 42\nname = \"lynxer\"\n[server]\nhost = \"localhost\"\n";

    // The whole document as JSON, and a single value.
    println(global.toml.tomlParse(text));
    println(global.toml.tomlGet(text, "server.host"));

    // Build a document from JSON.
    println(global.toml.tomlSerialize("{\"answer\":42,\"name\":\"lynxer\"}"));
}
```

---

## See also

- [Standard library contracts](../stdlib-contracts.md)
- [Built-in functions](../builtins.md)
- [Limitations](../limitations.md)
