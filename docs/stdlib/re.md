# re

Regular expressions over `std::regex` with Python-style pattern syntax.

**Backend:** native — `stdlib/re.so`, built from `stdlib/re.cpp` and the
translation layer in `stdlib/native_regex.hpp`. **Import:** `import("re")` →
`global.re.*`

Structured results (match lists, group lists, spans) are JSON strings.

| Function | Signature | Returns |
| --- | --- | --- |
| `test` | `(str pattern, str string)` | `true` if the pattern matches anywhere |
| `match` | `(str pattern, str string)` | Match anchored at the start, or `""` |
| `matchFull` | `(str pattern, str string)` | Full-string match, or `""` |
| `search` | `(str pattern, str string)` | First match anywhere, or `""` |
| `findall` | `(str pattern, str string)` | JSON array of all non-overlapping matches |
| `count` | `(str pattern, str string)` | Number of matches |
| `groups` | `(str pattern, str string)` | JSON array of capture groups of the first match |
| `groupsAll` | `(str pattern, str string)` | JSON array-of-arrays of groups per match |
| `named` | `(str pattern, str string)` | JSON object of named groups of the first match |
| `sub` | `(str pattern, str repl, str string)` | Replace every match |
| `subN` | `(str pattern, str repl, str string, int n)` | Replace the first `n` matches |
| `subn` | `(str pattern, str repl, str string)` | JSON `{"result": ..., "count": N}` |
| `split` | `(str pattern, str string)` | JSON array of the split parts |
| `splitN` | `(str pattern, str string, int maxSplit)` | Split at most `maxSplit` times |
| `escape` | `(str string)` | Regex-escaped literal text |
| `matchStart` | `(str pattern, str string)` | Start index of the first match, or `-1` |
| `matchEnd` | `(str pattern, str string)` | End index of the first match, or `-1` |
| `findSpans` | `(str pattern, str string)` | JSON array of `{start, end, match}` |
| `testIgnoreCase` | `(str pattern, str string)` | `test` with the `i` flag |
| `matchIgnoreCase` | `(str pattern, str string)` | `match` with the `i` flag |
| `searchIgnoreCase` | `(str pattern, str string)` | `search` with the `i` flag |
| `findallIgnoreCase` | `(str pattern, str string)` | `findall` with the `i` flag |
| `subIgnoreCase` | `(str pattern, str repl, str string)` | `sub` with the `i` flag |
| `findallMultiline` | `(str pattern, str string)` | `findall` with the `m` flag |
| `subMultiline` | `(str pattern, str repl, str string)` | `sub` with the `m` flag |
| `searchDotall` | `(str pattern, str string)` | `search` with `.` matching newlines |

Replacement strings support `\1` and `\g<name>` backreferences.

## Supported pattern syntax

`(?P<name>...)` and `(?P=name)` are translated; `(?i)`, `(?m)` and `(?s)` are
applied to the whole pattern. **Not supported** (they yield sentinel results
rather than raising): lookbehind `(?<=…)`/`(?<!…)`, atomic groups `(?>…)`, and
Unicode property escapes such as `\p{L}`.

## Example

```lynx
global setup(){ import("re"); }

global main(){
    println(global.re.findall("\\d+", "a1 b22 c333"));
    println(global.re.sub("(?P<w>\\w+)", "[\\g<w>]", "hi there"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
