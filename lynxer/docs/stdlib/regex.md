# regex

Extended regular-expression helpers with a named compiled-pattern cache.

**Backend:** native — `stdlib/regex.so`, built from `stdlib/regex.cpp` and
`stdlib/native_regex.hpp`. **Import:** `import("regex")` → `global.regex.*`

Compiled patterns live in a module-local cache keyed by name. This module
mirrors the Python reference's `re`-fallback path: the third-party `regex`
package is not available, so `findLetters`/`findDigits` are ASCII and
`findallOverlapping` is emulated.

| Function | Signature | Returns |
| --- | --- | --- |
| `compile` | `(str name, str pattern, str flags)` | Compiles and caches a pattern; flags are any of `I`, `M`, `S` (`X` is ignored) |
| `testCompiled` | `(str name, str string)` | `1` or `0` |
| `matchCompiled` | `(str name, str string)` | First match, or `""` |
| `findallCompiled` | `(str name, str string)` | JSON array of matches |
| `subCompiled` | `(str name, str repl, str string)` | Replaces all matches |
| `clearCache` | `()` | Empties the cache |
| `cacheKeys` | `()` | JSON array of cached pattern names |
| `isValid` | `(str pattern)` | `1` if the pattern compiles |
| `extract` | `(str pattern, str string)` | JSON object of named groups of the first match |
| `extractAll` | `(str pattern, str string)` | JSON array of named-group objects |
| `unique` | `(str pattern, str string)` | JSON array of distinct matches |
| `lastMatch` | `(str pattern, str string)` | Last match, or `""` |
| `findallOverlapping` | `(str pattern, str string)` | JSON array of overlapping matches |
| `replaceNth` | `(str pattern, str repl, str string, int n)` | Replaces only the `n`-th match, 1-indexed |
| `replaceAllLiteral` | `(str pattern, str repl, str string)` | Replaces all matches without backreference expansion |
| `highlight` | `(str pattern, str before, str after, str string)` | Wraps every match |
| `splitKeep` | `(str pattern, str string)` | JSON array that keeps the separators |
| `findLetters` | `(str string)` | JSON array of ASCII letter runs |
| `findDigits` | `(str string)` | JSON array of digit runs |
| `globToRegex` | `(str glob)` | Anchored regex equivalent of a glob |
| `countMatches` | `(str pattern, str string)` | Number of matches |
| `firstMatchPos` | `(str pattern, str string)` | Start index of the first match, or `-1` |
| `truncateMatch` | `(str pattern, str string, int maxLen)` | `maxLen`-character window centred on the first match |

Pattern syntax limits are the same as [re](re.md).

## Example

```lynx
global setup(){ import("regex"); }

global main(){
    global.regex.compile("digits", "\\d+", "");
    println(global.regex.testCompiled("digits", "abc123"));
    println(global.regex.findallCompiled("digits", "a1 b22"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [parity.md](../parity.md) — the parity scope with Python Lynxer.
- [limitations.md](../limitations.md) — the full divergence register.
