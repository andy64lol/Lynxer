# JS ABI and Node packages

Wynxer exposes a small, explicit **JS ABI** (`wynxer_js_abi_v1`) so `.wynx`
source can call JavaScript directly and use npm packages, without magic
coercion of Wynxer values into JS values. It is the web equivalent of Lynxer's
native-module ABI: a narrow, stable boundary to the host runtime.

## ABI builtins

| Wynxer call | Emitted JavaScript |
| --- | --- |
| `js("raw code");` | raw JS statement, verbatim |
| `jsExpr("expression")` | raw JS expression |
| `jsImport("pkg", "a, b")` | `import { a, b } from "pkg"` / `const { a, b } = require("pkg")` |
| `jsRequire("pkg")` | `require("pkg")` expression |
| `jsGlobal("name")` | a global JS binding reference |

The first argument of each builtin must be a **string literal**.

### Raw JS statements — `js(...)`

Use `js(...)` to interpolate JavaScript directly. At the top level it is
emitted before functions; inside a function it is emitted at that point:

```wynx
js("const API = 'https://example.com';");

fn onClick() {
    js("console.log('called from Wynxer');");
    int now = jsExpr("Date.now()");
    str version = jsGlobal("API");
}
```

Generated JS:

```js
const API = 'https://example.com';
function onClick() {
  console.log('called from Wynxer');
  var now = Date.now();
  var version = API;
}
```

### Package imports — `jsImport(...)`

`jsImport("pkg", "names")` calls are **hoisted** to module scope, so they can
appear inside any function. Named imports are comma-separated:

```wynx
fn f() {
    jsImport("lodash", "map, filter");
}
```

With `target = browser` (default) or `type = module`, this emits:

```js
import { map, filter } from "lodash";
```

With `target = node` and `type = commonjs`, it emits:

```js
const { map, filter } = require("lodash");
```

An empty name list produces a side-effect import (`import "pkg";`).

## Node / npm packages

### Installing packages

Wynxer drives Node's `npm` for you:

```bash
wynxer --add lodash chalk          # install + record in .wynxcfg deps
wynxer --add typescript --dev      # install + record in devDeps
wynxer --remove chalk              # uninstall + remove from .wynxcfg
```

`--add` / `--remove` run `npm install` / `npm uninstall` in the project
directory, update the `deps` / `devDeps` keys in `.wynxcfg`, and regenerate
`package.json`. Node.js/npm must be installed on your `PATH`.

> Note: put `--dev` **after** the package names (`wynxer --add pkg --dev`).

### `.wynxcfg` node keys

`wynxer init` scaffolds these keys. `wynxer build` keeps `package.json` in
sync with them:

| Key | Meaning | Default |
| --- | --- | --- |
| `deps` | npm packages (comma separated) | empty |
| `devDeps` | npm dev packages | empty |
| `node` | minimum Node version written to `engines` | `20` |
| `type` | JS module system (`commonjs` or `module`) | `commonjs` |
| `package_manager` | `npm`, `pnpm`, or `yarn` | `npm` |
| `target` | `browser` (default) or `node` (uses `type`) | `browser` |

A generated `package.json` looks like:

```json
{
  "name": "my-app",
  "version": "1.0.0",
  "private": true,
  "type": "commonjs",
  "engines": { "node": ">=20" },
  "dependencies": { "lodash": "latest" },
  "devDependencies": {}
}
```

## See also

- [CLI.md](CLI.md) — command reference
- [language.md](language.md) — the Wynxer language
- [wpf.md](wpf.md) — the page format
