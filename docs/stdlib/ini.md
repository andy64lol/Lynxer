# ini

INI parsing and serialization.

**Backend:** native — `stdlib/ini.so`, built from the Rust crate `rust/ini`
over `rust-ini`.

**Import:** `import("ini")` → `global.ini.*`

## How documents travel

An INI document crosses the module ABI as a **JSON string**, the shared
structured-value bridge (see [native-module-abi.md](../native-module-abi.md)):
an object of sections, each an object of **string** values. The unnamed leading
section uses `""` as its key. Values are strings in both directions.

A failure is a scalar sentinel: a document operation yields `""` and `iniValid`
yields `false`. An empty document is valid, so it parses to `{"":{}}`.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `iniParse` | `(str text)` | the document as JSON, or `""` |
| `iniSerialize` | `(str json)` | the INI document, or `""` |
| `iniValid` | `(str text)` | `true` / `false` |
| `iniGet` | `(str text, str section, str key)` | the value, or `""` |

## Example

```lynx
global setup(){ import("ini"); }

global main(){
    str text = "[server]\nhost = localhost\nport = 8080\n";

    println(global.ini.iniParse(text));
    println(global.ini.iniGet(text, "server", "host"));

    println(global.ini.iniSerialize("{\"server\":{\"host\":\"localhost\"}}"));
}
```

---

## See also

- [Standard library contracts](../stdlib-contracts.md)
- [Built-in functions](../builtins.md)
- [Limitations](../limitations.md)
