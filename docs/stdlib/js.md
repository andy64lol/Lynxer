# js

Run JavaScript using Node.js as a subprocess. Requires `node` on `PATH`.

Functions:

- `runJS(code)` → stdout of running `code` with Node or an error message.
- `runJSFile(path)` → stdout of running the `.js` file or `""` on error.
- `evalJS(expr)` → evaluates `expr` (wrapped with `console.log`) and returns output string.
- `nodeVersion()` → Node version string (e.g. `v20.11.0`) or `""` if not available.
- `nodeExists()` → `1` if node is available, otherwise `0`.

Notes:
- Long-running or blocking JS may be terminated by the subprocess timeout (10–30s depending on call).