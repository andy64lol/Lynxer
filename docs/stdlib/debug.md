# debug

Runtime inspection, assertions, structured logging, and timing helpers.

```c
global setup(){
    import("debug");
}
```

---

## Assertions

Assertions raise a runtime error with a clear message when a condition is not met.
Use them to catch bugs early.

| Function | Signature | Description |
|----------|-----------|-------------|
| `assert` | `assert(bool cond, str msg)` | Raise `AssertionError(msg)` if `cond` is `false`. |
| `assertEq` | `assertEq(str a, str b, str msg)` | Raise if `a != b` (string comparison). Message includes both values. |
| `assertNotEq` | `assertNotEq(str a, str b, str msg)` | Raise if `a == b`. |
| `assertGt` | `assertGt(float a, float b, str msg)` | Raise if `a ≤ b`. |
| `assertLt` | `assertLt(float a, float b, str msg)` | Raise if `a ≥ b`. |
| `assertContains` | `assertContains(str haystack, str needle, str msg)` | Raise if `needle` is not a substring of `haystack`. |

### Example

```c
global setup(){
    import("debug");
}

global main(){
    int x = 42;
    global.debug.assert(x > 0, "x must be positive");     // passes

    str name = "Lynxer";
    global.debug.assertEq(name, "Lynxer", "name mismatch");   // passes
    global.debug.assertContains("Hello, Lynxer!", "Lynxer", "greeting missing name");

    global.debug.assertGt(10.0, 5.0, "10 should be > 5");     // passes
}
```

---

## Type inspection

| Function | Signature | Description |
|----------|-----------|-------------|
| `typeOf` | `typeOf(any val)` | Return the Lynxer type name as a string: `"int"`, `"float"`, `"str"`, `"bool"`, `"list"`, `"null"`. |
| `dump` | `dump(any val)` | Print `[debug.dump] type=... value=...` to stdout. |
| `inspect` | `inspect(any val)` | Return a JSON string `{"type":"...","value":"..."}`. |
| `pp` | `pp(any val)` | Pretty-print `val` to stdout (alias for `dump`). |

### Example

```c
global main(){
    int n = 7;
    print(global.debug.typeOf(n)); print("\n");    // int
    global.debug.dump(n);                          // [debug.dump] type=int  value=7

    str info = global.debug.inspect("hello");
    print(info); print("\n");    // {"type": "str", "value": "hello"}
}
```

---

## Structured logging

All log functions write a formatted line to stdout. Levels are purely cosmetic — no filtering is applied at runtime.

| Function | Signature | Description |
|----------|-----------|-------------|
| `log` | `log(str msg)` | Generic log: `[LOG] msg`. |
| `info` | `info(str msg)` | Info-level: `[INFO] msg`. |
| `warn` | `warn(str msg)` | Warning: `[WARN] msg`. |
| `error` | `error(str msg)` | Error: `[ERROR] msg`. |
| `debug` | `debug(str msg)` | Debug trace: `[DEBUG] msg`. |

### Example

```c
global main(){
    global.debug.info("server starting");
    global.debug.warn("config file not found, using defaults");
    global.debug.error("failed to open socket");
    global.debug.debug("entered loop iteration 3");
}
```

Output:
```
[INFO] server starting
[WARN] config file not found, using defaults
[ERROR] failed to open socket
[DEBUG] entered loop iteration 3
```

---

## Timers

| Function | Signature | Description |
|----------|-----------|-------------|
| `startTimer` | `startTimer(str name)` | Start (or restart) a named timer. |
| `stopTimer` | `stopTimer(str name)` | Stop the named timer and return elapsed milliseconds as a float. |
| `elapsed` | `elapsed(str name)` | Milliseconds since `startTimer(name)` without stopping it. |
| `clock` | `clock()` | High-resolution process time in milliseconds (`perf_counter * 1000`). |

### Example

```c
global main(){
    global.debug.startTimer("loop");

    int i = 0;
    for(i = 0; i < 100000; i = i + 1){}

    float ms = global.debug.stopTimer("loop");
    print("loop took "); print(ms); print(" ms\n");

    float t = global.debug.clock();
    print("process clock: "); print(t); print(" ms\n");
}
```

---

## Environment helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `envGet` | `envGet(str key)` | Value of environment variable `key`, or `""` if not set. |
| `envAll` | `envAll()` | All environment variables as a JSON object string. |
| `getMemory` | `getMemory()` | Current process RSS in megabytes (float), or `-1.0` on error. |

### Example

```c
global setup(){
    import("debug");
    import("json");
}

global main(){
    str path = global.debug.envGet("PATH");
    print(path); print("\n");

    float mb = global.debug.getMemory();
    print("memory: "); print(mb); print(" MB\n");
}
```

---

## Notes

- `typeOf` maps Python internals to Lynxer types: `bool` is checked before `int` because Python's `bool` is a subclass of `int`.
- Timers are stored by name in a global table — `stopTimer` does not remove them, so you can call `elapsed` after `stopTimer`.
- `getMemory` tries `resource.getrusage` (Unix) and falls back to `psutil` if available, then returns `-1.0`.
