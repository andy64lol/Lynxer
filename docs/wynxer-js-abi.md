# Wynxer JS ABI and Node packages

Wynxer exposes a small, explicit **JS ABI** (`wynxer_js_abi_v1`) so `.wynx`
source can call JavaScript directly and use npm packages. It is the web
equivalent of Lynxer's native-module ABI: a narrow, stable boundary to the
host runtime, with no magic coercion of Wynxer values.

## ABI builtins

| Wynxer call | Emitted JavaScript |
| --- | --- |
| `js("raw code");` | raw JS statement, verbatim |
| `jsExpr("expression")` | raw JS expression |
| `jsImport("pkg", "a, b")` | `import { a, b } from "pkg"` / `const { a, b } = require("pkg")` |
| `jsRequire("pkg")` | `require("pkg")` expression |
| `jsGlobal("name")` | a global JS binding reference |

The first argument of each builtin must be a **string literal**.

### Raw JS

`js(...)` at the top level is emitted before functions; inside a function it
is emitted at that point:

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

### Package imports

`jsImport("lodash", "map, filter")` calls are hoisted to module scope. With
`target = browser` or `type = module` they emit ES imports; with
`target = node` and `type = commonjs` they emit `require` destructuring.

## Node/npm packages

Wynxer drives npm for you:

```bash
wynxer --add lodash chalk          # npm install + record in .wynxcfg deps
wynxer --add typescript --dev      # save as devDependencies
wynxer --remove chalk              # npm uninstall + update config
```

Put `--dev` after the package names. Node.js/npm must be on `PATH`.

The `.wynxcfg` node keys are `deps`, `devDeps`, `node` (minimum version),
`type` (`commonjs` or `module`), `package_manager` and `target`. `wynxer
build` keeps `package.json` in sync:

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
