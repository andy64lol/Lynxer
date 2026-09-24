# regex

Extended regular expression operations with compiled pattern caching.

Uses the `regex` package when installed (`pip install regex`), falling back to Python's
built-in `re` automatically. The `regex` package adds: possessive quantifiers (`++`, `*+`),
atomic groups (`(?>...)`), Unicode category escapes (`\p{L}`), and overlapping matches.

> **Tip:** For basic one-off regex work (test, match, findall, sub, split, groups), use
> `global.re.*` from `re.lynx`. Import `regex` when you need compiled pattern caching,
> advanced Unicode support, or the extra helpers below.

```c
global setup(){
    import("regex");
}
```

---

## Compiled pattern cache

Compile a pattern once, reuse it many times. The cache is stored in memory for the
lifetime of the program.

| Function | Signature | Description |
|----------|-----------|-------------|
| `compile` | `compile(str name, str pattern, str flags)` | Compile `pattern` with `flags` and store under `name`. `flags` is any combination of `"I"` (ignore case), `"M"` (multiline), `"S"` (dotall), `"X"` (verbose). Pass `""` for no flags. |
| `testCompiled` | `testCompiled(str name, str string)` | `true` if the named pattern matches anywhere in `string`. |
| `matchCompiled` | `matchCompiled(str name, str string)` | First match of the named pattern, or `""`. |
| `findallCompiled` | `findallCompiled(str name, str string)` | JSON array of all matches. |
| `subCompiled` | `subCompiled(str name, str repl, str string)` | Replace all matches of the named pattern with `repl`. |
| `clearCache` | `clearCache()` | Delete all compiled patterns from the cache. |
| `isValid` | `isValid(str pattern)` | `true` if `pattern` is a valid regex (does not raise on compile). |

### Example — compile once, reuse

```c
global setup(){
    import("regex");
}

global main(){
    // compile an email pattern once
    global.regex.compile("email", "[a-zA-Z0-9._%+\\-]+@[a-zA-Z0-9.\\-]+\\.[a-zA-Z]{2,}", "");

    print(global.regex.testCompiled("email", "contact@example.com")); print("\n");   // true
    print(global.regex.testCompiled("email", "not-an-email")); print("\n");          // false

    str all = global.regex.findallCompiled("email", "a@b.com and c@d.org");
    print(all); print("\n");   // ["a@b.com","c@d.org"]

    global.regex.clearCache();
}
```

---

## Pattern validation

| Function | Signature | Description |
|----------|-----------|-------------|
| `isValid` | `isValid(str pattern)` | `true` if the pattern compiles without error. |

```c
global main(){
    print(global.regex.isValid("[a-z]+")); print("\n");     // true
    print(global.regex.isValid("[unclosed")); print("\n");  // false
}
```

---

## Extended extraction

| Function | Signature | Description |
|----------|-----------|-------------|
| `extract` | `extract(str pattern, str string)` | Return the first match of `pattern` in `string`, or `""`. |
| `extractAll` | `extractAll(str pattern, str string)` | JSON array of all non-overlapping matches. |
| `unique` | `unique(str pattern, str string)` | JSON array of unique matches (duplicates removed, order preserved). |

### Example

```c
global main(){
    str text = "foo 123 bar 456 baz 123";
    str first = global.regex.extract("\\d+", text);
    print(first); print("\n");    // 123

    str all = global.regex.extractAll("\\d+", text);
    print(all); print("\n");      // ["123","456","123"]

    str uniq = global.regex.unique("\\d+", text);
    print(uniq); print("\n");     // ["123","456"]
}
```

---

## Replace helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `replaceNth` | `replaceNth(str pattern, str repl, str string, int n)` | Replace only the `n`-th match (1-based). Returns unchanged string if `n` is out of range. |
| `replaceAllLiteral` | `replaceAllLiteral(str pattern, str repl, str string)` | Replace all matches with `repl` treated as a literal string (no back-reference expansion). |

### Example

```c
global main(){
    str s = "cat cat cat";

    // replace only the 2nd occurrence
    str r = global.regex.replaceNth("cat", "dog", s, 2);
    print(r); print("\n");    // cat dog cat

    // literal replacement — "$" in repl is not a back-reference
    str t = global.regex.replaceAllLiteral("\\d+", "$VALUE$", "a1 b2 c3");
    print(t); print("\n");    // a$VALUE$ b$VALUE$ c$VALUE$
}
```

---

## Search helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `lastMatch` | `lastMatch(str pattern, str string)` | Last (rightmost) match of `pattern`, or `""`. |
| `highlight` | `highlight(str pattern, str string, str open, str close)` | Wrap every match in `string` with `open` and `close` delimiters. |
| `splitKeep` | `splitKeep(str pattern, str string)` | Split `string` by `pattern` but keep the separators in the result. Returns JSON array. |

### Example

```c
global main(){
    str s = "one 1 two 2 three 3";
    print(global.regex.lastMatch("\\d+", s)); print("\n");   // 3

    // highlight numbers in ANSI bold (or any markers)
    str h = global.regex.highlight("\\d+", s, "[", "]");
    print(h); print("\n");   // one [1] two [2] three [3]

    // split keeping separators
    str parts = global.regex.splitKeep(",", "a,b,c");
    print(parts); print("\n");   // ["a",",","b",",","c"]
}
```

---

## Utility

| Function | Signature | Description |
|----------|-----------|-------------|
| `globToRegex` | `globToRegex(str glob)` | Convert a glob pattern (`*`, `?`, `[...]`) to a regex string. |
| `countWords` | `countWords(str string)` | Count words (sequences of `\w+`) in `string`. |
| `truncateMatch` | `truncateMatch(str pattern, str string, int maxLen)` | Find the first match and truncate it to `maxLen` characters. |

### Example

```c
global main(){
    // convert a glob to a regex and test it
    str rx = global.regex.globToRegex("*.lynx");
    print(rx); print("\n");                      // .*\.lynx
    global.regex.compile("lynxfiles", rx, "");
    print(global.regex.testCompiled("lynxfiles", "main.lynx")); print("\n");   // true

    print(global.regex.countWords("Hello, World! 123")); print("\n");   // 3

    str m = global.regex.truncateMatch("\\w{10,}", "superlongword yes short", 6);
    print(m); print("\n");   // superl
}
```

---

## Notes

- When the `regex` package is not installed, all functions silently fall back to Python's built-in `re`. Most patterns work identically; advanced features like `\p{L}` will produce an error from `re`.
- The pattern cache survives across functions but is scoped to the program run. Call `clearCache()` if you need to free memory.
- `replaceNth` with `n=1` is equivalent to replacing the first match once.
