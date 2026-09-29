# yaml

YAML parsing and serialization.

**Backend:** native — `stdlib/yaml.so`, built from the Rust crate `rust/yaml`
over `serde_yml`.

**Import:** `import("yaml")` → `global.yaml.*`

## How documents travel

A YAML document crosses the module ABI as a **JSON string**, the shared
structured-value bridge (see [native-module-abi.md](../native-module-abi.md)): a
mapping becomes an object and a sequence an array. A mapping whose key is not a
string (which JSON cannot hold) is a failure.

Anchors and aliases are resolved while parsing, so the input is capped at 1 MiB
to bound an alias-expansion payload; a larger input is a failure.

A failure is a scalar sentinel: a document operation yields `""` and
`yamlValid` yields `false`.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `yamlParse` | `(str text)` | the document as JSON, or `""` |
| `yamlSerialize` | `(str json)` | the YAML document, or `""` |
| `yamlValid` | `(str text)` | `true` / `false` |
| `yamlGet` | `(str text, str key)` | the value at a dotted key as JSON, or `""` |

## Example

```lynx
global setup(){ import("yaml"); }

global main(){
    str text = "name: lynxer\nitems:\n  - 1\n  - 2\n";

    println(global.yaml.yamlParse(text));
    println(global.yaml.yamlGet(text, "name"));

    println(global.yaml.yamlSerialize("{\"name\":\"lynxer\",\"items\":[1,2]}"));
}
```

---

## See also

- [Standard library contracts](../stdlib-contracts.md)
- [Built-in functions](../builtins.md)
- [Limitations](../limitations.md)
