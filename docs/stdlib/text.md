# text

String helpers built on the core string builtins.

**Backend:** pure — `stdlib/text.lynx` only. **Import:** `import("text")` →
`global.text.*`

The interpreter's strings are byte strings, so length, indexing and slicing here
count bytes.

## Search and replace

| Function | Signature | Notes |
| --- | --- | --- |
| `contains` | `(str value, str needle) -> bool` | Substring test (`true` for an empty needle) |
| `startsWith` / `endsWith` | `(str value, str affix) -> bool` | Prefix / suffix test |
| `indexOf` | `(str value, str needle) -> int` | First index, or `-1`; `0` for an empty needle |
| `count` | `(str value, str needle) -> int` | Non-overlapping occurrences |
| `replace` | `(str value, str old, str new) -> str` | Replaces every occurrence |
| `replaceFirst` | `(str value, str old, str new) -> str` | Replaces the first occurrence |
| `remove` | `(str value, str target) -> str` | Removes every occurrence |

`contains` here is a *substring* test, unlike the core `contains` builtin, which
only tests list/tuple membership.

## Case

| Function | Signature | Notes |
| --- | --- | --- |
| `upper` / `lower` | `(str value) -> str` | Case conversion |
| `capitalize` | `(str value) -> str` | Upper-case the first byte, lower-case the rest |
| `title` | `(str value) -> str` | Capitalise the first letter of each word |
| `swapCase` | `(str value) -> str` | Swap upper/lower per byte |

## Whitespace and repetition

| Function | Signature | Notes |
| --- | --- | --- |
| `trim` | `(str value) -> str` | Removes leading and trailing whitespace |
| `trimLeft` / `trimRight` | `(str value) -> str` | Removes leading / trailing whitespace |
| `repeat` | `(str value, int count) -> str` | Repeats `count` times |
| `spaces` | `(int count) -> str` | `count` space characters |
| `expandTabs` | `(str value, int tabSize) -> str` | Expand tabs to `tabSize`-wide stops |
| `wordWrap` | `(str value, int width) -> str` | Greedy wrap on spaces |
| `indent` | `(str value, str prefix) -> str` | Prefix every line |

## Predicates

| Function | Signature | Notes |
| --- | --- | --- |
| `isEmpty` | `(str value) -> bool` | Zero length |
| `isBlank` | `(str value) -> bool` | Only whitespace |
| `isAlpha` | `(str value) -> bool` | Every byte is a letter; `false` for `""` |
| `isNumeric` | `(str value) -> bool` | Every byte is a digit; `false` for `""` |
| `isAlphaNumeric` | `(str value) -> bool` | Every byte is alphanumeric; `false` for `""` |

## Padding and slicing

| Function | Signature | Notes |
| --- | --- | --- |
| `padLeft` / `padRight` / `center` | `(str value, int width, str fill) -> str` | Pad to `width` |
| `zfill` | `(str value, int width) -> str` | Zero-pad, keeping a leading sign |
| `substring` | `(str value, int start, int end) -> str` | Half-open slice (builtin forwarder) |
| `charAt` | `(str value, int index) -> str` | One-byte string (builtin forwarder) |
| `reverse` | `(str value) -> str` | Reverse the bytes |

## Splitting and joining

| Function | Signature | Notes |
| --- | --- | --- |
| `split` | `(str value, str separator) -> list` | Split into a list |
| `splitLines` | `(str value) -> list` | Split on newlines (`\r\n` aware) |
| `splitFirst` | `(str value, str separator) -> list` | Two-element `[before, after]` |
| `join` | `(list parts, str separator) -> str` | Join a list |

## Example

```lynx
global setup(){ import("text"); }

global main(){
    println(global.text.reverse("abc"));        // cba
    println(global.text.contains("hello", "ell")); // true
    println(global.text.count("banana", "na")); // 2
    println(global.text.indexOf("banana", "na")); // 2
    println(global.text.zfill("42", 5));        // 00042
    println(global.text.splitFirst("a=b", "=")); // [a, b]
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
