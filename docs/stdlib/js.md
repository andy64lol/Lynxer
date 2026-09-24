# js

Run JavaScript through a Node.js subprocess.

**Backend:** native — `stdlib/js.so`, built from `stdlib/js.cpp`.
**Import:** `import("js")` → `global.js.*`

Requires `node` on `PATH`; when it is missing, every runner returns
`"Error: node not found on PATH"`. No timeout is applied, and `stderr` is
inherited rather than captured.

| Function | Signature | Notes |
| --- | --- | --- |
| `runJS` | `(str code) -> str` | Writes the code to a temporary `.js` file, runs it, returns stdout |
| `runJSFile` | `(str path) -> str` | Runs a `.js` file and returns stdout |
| `evalJS` | `(str expr) -> str` | Wraps `expr` in `console.log(...)`, returns the trimmed result |
| `nodeVersion` | `() -> str` | `node --version`, or `""` |
| `nodeExists` | `() -> int` | `1` when `node` runs |

## Example

```lynx
global setup(){ import("js"); }

global main(){
    println(global.js.evalJS("1 + 2"));
    println(global.js.runJS("console.log('hello from js');"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
