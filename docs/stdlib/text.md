# text

Basic string helpers.

**Backend:** pure — `stdlib/text.lynx` only, written on top of the core string
builtins. **Import:** `import("text")` → `global.text.*`

| Function | Signature | Notes |
| --- | --- | --- |
| `reverse` | `(str value) -> str` | Reverses the string |
| `repeat` | `(str value, int count) -> str` | Repeats `count` times |
| `trim` | `(str value) -> str` | Removes leading and trailing whitespace |
| `contains` | `(str value, str needle) -> bool` | Substring test (`true` for an empty needle) |
| `startsWith` | `(str value, str prefix) -> bool` | Prefix test |
| `endsWith` | `(str value, str suffix) -> bool` | Suffix test |
| `count` | `(str value, str needle) -> int` | Non-overlapping occurrences |
| `replace` | `(str value, str oldValue, str newValue) -> str` | Replaces every occurrence |
| `upper` / `lower` | `(str value) -> str` | Case conversion |
| `isEmpty` | `(str value) -> bool` | Zero length |
| `isBlank` | `(str value) -> bool` | Only whitespace |

Note that `contains` here is a *substring* test, unlike the core `contains`
builtin, which only tests list/tuple membership.

## Example

```lynx
global setup(){ import("text"); }

global main(){
    println(global.text.reverse("abc"));
    println(global.text.contains("hello", "ell"));
    println(global.text.count("banana", "na"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
