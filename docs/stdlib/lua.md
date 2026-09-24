# lua

Run Lua code inside Lynxer with the embedded [`lupa`](https://pypi.org/project/lupa/)
runtime. Lua does not need to be installed separately or available on `PATH`.

Install the dependency in the project virtual environment with:

```bash
pip install lupa
```

The standard `make build` target installs `lupa` from
`requirements_venv.txt`.

Functions:

- `runLua(code)` → captured output from Lua `print(...)` calls, or an error string.
- `runLuaFile(path)` → captured output from a Lua file, or an error string.
- `evalLua(expr)` → evaluates one Lua expression and returns its result as a string.
- `luaVersion()` → embedded Lua version string (for example, `"Lua 5.5"`), or `""`
  when `lupa` is unavailable.
- `luaExists()` → `1` when `lupa` can be imported, otherwise `0`.

Import and use it like the other execution modules:

```lynx
global setup(){
    import("lua");
}

global main(){
    str output = global.lua.runLua("print('hello from Lua')");
    print(output);

    print(global.lua.evalLua("2 + 3")); print("\n");
    print(global.lua.luaVersion()); print("\n");
}
```

Each call creates a fresh Lua runtime, so variables do not persist between
`runLua`, `runLuaFile`, and `evalLua` calls.