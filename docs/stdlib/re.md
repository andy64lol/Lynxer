# re

Regular expression operations wrapping Python's `re` module. All pattern strings use Python regex syntax.

Functions that return multiple matches encode them as **JSON arrays**; use the `json` module to parse them if needed.

```c
global setup(){ import("re"); }

global main(){
    bool found = global.re.test("\\d+", "abc123");   // true
    str  first = global.re.search("\\d+", "abc123"); // "123"
    str  all   = global.re.findall("\\d+", "1a2b3"); // '["1","2","3"]'
    println(found);
    println(first);
    println(all);
}
```

---

## Basic matching

| Function | Signature | Description |
|----------|-----------|-------------|
| `test` | `test(str pattern, str string)` | `true` if `pattern` matches anywhere in `string`. |
| `match` | `match(str pattern, str string)` | Match at the **start** of `string`. Returns matched substring or `""`. |
| `matchFull` | `matchFull(str pattern, str string)` | Match pattern against the **entire** string. Returns matched substring or `""`. |
| `search` | `search(str pattern, str string)` | Find the **first** occurrence anywhere. Returns matched substring or `""`. |
| `findall` | `findall(str pattern, str string)` | Return a **JSON array** of all non-overlapping matches. |
| `count` | `count(str pattern, str string)` | Count non-overlapping matches. |

```c
println(global.re.test("^hello", "hello world"));    // true
println(global.re.match("\\d+", "42 apples"));       // "42"
println(global.re.matchFull("\\d{3}", "123"));        // "123"
println(global.re.matchFull("\\d{3}", "1234"));       // ""
println(global.re.search("[A-Z]+", "hello WORLD"));  // "WORLD"
println(global.re.findall("[aeiou]", "hello"));       // '["e","o"]'
println(global.re.count("\\d", "a1b2c3"));            // 3
```

---

## Capture groups

| Function | Signature | Description |
|----------|-----------|-------------|
| `groups` | `groups(str pattern, str string)` | JSON array of capture groups from the **first** match. Returns `"[]"` if no match. |
| `groupsAll` | `groupsAll(str pattern, str string)` | JSON array-of-arrays of groups for **every** match. |
| `named` | `named(str pattern, str string)` | JSON object of **named** groups from the first match. Use `(?P<name>…)` syntax. |

```c
// groups
str g = global.re.groups("(\\w+)@(\\w+)", "user@host");
// '["user","host"]'

// groupsAll
str ga = global.re.groupsAll("(\\w+)=(\\d+)", "x=1 y=2 z=3");
// '[["x","1"],["y","2"],["z","3"]]'

// named groups
str n = global.re.named("(?P<year>\\d{4})-(?P<month>\\d{2})", "2024-07");
// '{"year":"2024","month":"07"}'
```

---

## Substitution

| Function | Signature | Description |
|----------|-----------|-------------|
| `sub` | `sub(str pattern, str repl, str string)` | Replace **all** occurrences of `pattern` with `repl`. Backreferences `\1`, `\g<name>` work. |
| `subN` | `subN(str pattern, str repl, str string, int n)` | Replace the first `n` occurrences only. |
| `subn` | `subn(str pattern, str repl, str string)` | Replace all; return JSON `{"result":"…","count":N}`. |

```c
println(global.re.sub("\\d+", "NUM", "a1 b22 c333"));
// "a NUM b NUM c NUM"

println(global.re.subN("\\d+", "X", "1a2b3c", 2));
// "Xa Xb 3c"

str r = global.re.subn("[aeiou]", "*", "hello world");
// '{"result":"h*ll* w*rld","count":3}'
```

---

## Splitting

| Function | Signature | Description |
|----------|-----------|-------------|
| `split` | `split(str pattern, str string)` | Split by every match of `pattern`. Returns JSON array. |
| `splitN` | `splitN(str pattern, str string, int maxSplit)` | Split at most `maxSplit` times. Returns JSON array. |

```c
println(global.re.split("\\s+", "one  two   three"));
// '["one","two","three"]'

println(global.re.splitN(",", "a,b,c,d", 2));
// '["a","b","c,d"]'
```

---

## Utility

| Function | Signature | Description |
|----------|-----------|-------------|
| `escape` | `escape(str string)` | Escape all regex special characters so `string` matches literally. |
| `matchStart` | `matchStart(str pattern, str string)` | Start index of the first match, or `-1`. |
| `matchEnd` | `matchEnd(str pattern, str string)` | End index of the first match, or `-1`. |
| `findSpans` | `findSpans(str pattern, str string)` | JSON array of `{"start":N,"end":N,"match":"…"}` for every match. |

```c
println(global.re.escape("a.b+c?"));    // "a\.b\+c\?"
println(global.re.matchStart("\\d+", "abc123def"));  // 3
println(global.re.matchEnd("\\d+",   "abc123def"));  // 6
println(global.re.findSpans("\\d+", "a1 bb22 c333"));
// '[{"start":1,"end":2,"match":"1"},{"start":4,"end":6,"match":"22"},...]'
```

---

## Case-insensitive variants

| Function | Signature | Description |
|----------|-----------|-------------|
| `testIgnoreCase` | `testIgnoreCase(str pattern, str string)` | `test` with `IGNORECASE` flag. |
| `matchIgnoreCase` | `matchIgnoreCase(str pattern, str string)` | `match` with `IGNORECASE`. |
| `searchIgnoreCase` | `searchIgnoreCase(str pattern, str string)` | `search` with `IGNORECASE`. |
| `findallIgnoreCase` | `findallIgnoreCase(str pattern, str string)` | `findall` with `IGNORECASE`. |
| `subIgnoreCase` | `subIgnoreCase(str pattern, str repl, str string)` | `sub` with `IGNORECASE`. |

```c
println(global.re.testIgnoreCase("hello", "Say HELLO!"));     // true
println(global.re.findallIgnoreCase("[aeiou]", "Hello"));     // '["e","o"]'
println(global.re.subIgnoreCase("foo", "bar", "FOO foo Foo")); // "bar bar bar"
```

---

## Multiline & DOTALL variants

| Function | Signature | Description |
|----------|-----------|-------------|
| `findallMultiline` | `findallMultiline(str pattern, str string)` | `findall` with `MULTILINE` flag (`^`/`$` match each line). |
| `subMultiline` | `subMultiline(str pattern, str repl, str string)` | `sub` with `MULTILINE`. |
| `searchDotall` | `searchDotall(str pattern, str string)` | `search` with `DOTALL` (`.` matches newlines). |

```c
str text = "line1\nline2\nline3";
println(global.re.findallMultiline("^line\\d", text));
// '["line1","line2","line3"]'

str html = "<div>\nsome\ntext\n</div>";
println(global.re.searchDotall("<div>(.*)</div>", html));
// "<div>\nsome\ntext\n</div>"
```

---

## Common patterns reference

| Goal | Pattern |
|------|---------|
| Integer | `\\d+` |
| Float | `\\d+\\.\\d+` |
| Email | `[\\w.+-]+@[\\w-]+\\.[\\w.]+` |
| URL | `https?://[^\\s]+` |
| ISO date | `\\d{4}-\\d{2}-\\d{2}` |
| Whitespace | `\\s+` |
| Word boundary | `\\bword\\b` |
| Hex colour | `#[0-9a-fA-F]{6}` |
| IPv4 address | `\\d{1,3}(\\.\\d{1,3}){3}` |

> **Escape reminder:** In Lynxer string literals `\\` produces a single backslash, so `\\d` in a Lynxer string becomes `\d` in the regex engine — exactly what Python expects.
