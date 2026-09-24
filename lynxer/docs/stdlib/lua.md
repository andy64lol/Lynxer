# lua

Embedded Lua 5.4 execution backed by Rust `mlua` with vendored Lua sources.
The module is a `cdylib` installed as `stdlib/lua.so`; it does not require a
system Lua installation.

> **Requires:** a Rust toolchain (`cargo`). Build it with `make cargo` or
> `make buildLynxer`.

```lynx
global setup(){ import("lua"); }

global main(){
    println(global.lua.luaVersion());
    println(global.lua.evalLua("1 + 2 * 3"));
    println(global.lua.runLua("print('hello from Lua')"));
}
```

| Function | Description |
|---|---|
| `runLua(code)` | Execute an in-memory Lua script and return captured `print` output. |
| `runLuaFile(path)` | Read and execute a Lua file and return captured `print` output. |
| `evalLua(expression)` | Evaluate one Lua expression and return its scalar text representation. |
| `luaVersion()` | Return `Lua 5.4`. |
| `luaExists()` | Return `true` when the embedded runtime is available. |

Lua `print` arguments are joined with tabs. Each captured output string ends
with a newline when the script printed at least one line. Runtime and file
errors are returned as strings beginning with `Error:`.

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [parity.md](../parity.md) — the parity scope with Python Clynxer.
- [limitations.md](../limitations.md) — the full divergence register.
