# Standard Library

Lynxer ships a set of standard library modules in the `stdlib/` folder. Import any of them inside `setup()`:

```c
global setup(){
    import("math");
    import("typing");
    import("fileIO");
    import("csv");
    import("shell");
    import("os");
    import("path");
    import("json");
    import("js");
    import("lua");
    import("tui");
    import("http");
    import("net");
    import("server");
    import("sys");
    import("re");
    import("regex");
    import("random");
    import("time");
    import("debug");
    import("colorlib");
    import("tkinter");
    import("turtle");
    import("game");
    import("multiprocessing");
    import("sqldb");
}
```

All functions are accessed via `global.<module>.<function>(...)`.

See the [stdlib/ documentation folder](stdlib/README.md) for individual module pages.

---

## multiprocessing

Run shell commands in parallel using Python's `multiprocessing` and `threading` modules.

See [docs/multiprocessing.md](multiprocessing.md) for the full reference.

| Function | Signature | Description |
|----------|-----------|-------------|
| `workerCount` | `workerCount()` | Number of available CPU cores |
| `runParallel` | `runParallel(list commands)` | Run shell commands in parallel; return list of outputs |
| `mapShell` | `mapShell(str template, list items)` | Run command per item (replace `{}` with item); return outputs |
| `threadMap` | `threadMap(str template, list items)` | Like `mapShell` but uses threads (better for I/O-bound work) |
| `runParallelSilent` | `runParallelSilent(list commands)` | Run commands in parallel, discard output; return exit codes |

---

## math

Mathematical operations wrapping Python's `math`, `random`, and **NumPy** modules.

Requires: `pip install numpy` (for NumPy-backed functions).

See [docs/stdlib/math.md](stdlib/math.md) for the full reference.

**Core (Python `math` / pure Lynxer)**

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs` | `abs(float n)` | Absolute value |
| `max` | `max(float a, float b)` | Larger of two values |
| `min` | `min(float a, float b)` | Smaller of two values |
| `clamp` | `clamp(float val, float lo, float hi)` | Clamp `val` to `[lo, hi]` (int) |
| `clampFloat` | `clampFloat(float val, float lo, float hi)` | Clamp `val` to `[lo, hi]` (float) |
| `pow` | `pow(int base, int exp)` | Integer exponentiation |
| `sqrt` | `sqrt(float n)` | Square root |
| `floor` | `floor(float n)` | Round down |
| `ceil` | `ceil(float n)` | Round up |
| `round` | `round(float n)` | Round to nearest integer |
| `roundNum` | `roundNum(float n)` | Alias for `round` (legacy) |
| `pi` | `pi()` | `3.141592653589793` |
| `e` | `e()` | `2.718281828459045` |
| `log` | `log(float n)` | Natural logarithm |
| `log2` | `log2(float n)` | Base-2 logarithm |
| `log10` | `log10(float n)` | Base-10 logarithm |
| `sin` | `sin(float n)` | Sine (radians) |
| `cos` | `cos(float n)` | Cosine (radians) |
| `tan` | `tan(float n)` | Tangent (radians) |
| `degrees` | `degrees(float n)` | Radians → degrees |
| `radians` | `radians(float n)` | Degrees → radians |
| `sign` | `sign(float n)` | `-1`, `0`, or `1` |
| `isEven` | `isEven(int n)` | `true` if even |
| `isOdd` | `isOdd(int n)` | `true` if odd |
| `factorial` | `factorial(int n)` | `n!` |
| `gcd` | `gcd(int a, int b)` | Greatest common divisor |
| `lcm` | `lcm(int a, int b)` | Least common multiple |
| `hypot` | `hypot(float a, float b)` | `sqrt(a² + b²)` |
| `truncate` | `truncate(float n)` | Remove fractional part |
| `isqrt` | `isqrt(int n)` | Integer square root |
| `isPrime` | `isPrime(int n)` | `true` if prime |
| `nextPrime` | `nextPrime(int n)` | Smallest prime > `n` |
| `binomial` | `binomial(int n, int k)` | `C(n, k)` |
| `sumRange` | `sumRange(int lo, int hi)` | Sum of integers `lo..hi` |
| `lerp` | `lerp(float lo, float hi, float t)` | Linear interpolation |
| `mapRange` | `mapRange(value, inLo, inHi, outLo, outHi)` | Map between ranges |
| `randInt` | `randInt(int lo, int hi)` | Random integer in `[lo, hi]` |
| `randFloat` | `randFloat(float lo, float hi)` | Random float in `[lo, hi)` |

**NumPy-backed — scalars**

| Function | Signature | Description |
|----------|-----------|-------------|
| `tau` | `tau()` | `2π ≈ 6.283…` |
| `exp` | `exp(float n)` | `eⁿ` |
| `arcsin` | `arcsin(float n)` | Inverse sine (radians) |
| `arccos` | `arccos(float n)` | Inverse cosine (radians) |
| `arctan` | `arctan(float n)` | Inverse tangent (radians) |
| `arctan2` | `arctan2(float y, float x)` | Four-quadrant `atan(y/x)` |
| `sinh` | `sinh(float n)` | Hyperbolic sine |
| `cosh` | `cosh(float n)` | Hyperbolic cosine |
| `tanh` | `tanh(float n)` | Hyperbolic tangent |
| `roundTo` | `roundTo(float n, int decimals)` | Round to `decimals` places |
| `mean` | `mean(list lst)` | Arithmetic mean of a list |
| `median` | `median(list lst)` | Median of a list |
| `std` | `std(list lst)` | Population standard deviation |
| `variance` | `variance(list lst)` | Population variance |
| `percentile` | `percentile(list lst, float p)` | p-th percentile (p in 0–100) |
| `corrcoef` | `corrcoef(any a, any b)` | Pearson correlation coefficient |
| `prod` | `prod(list lst)` | Product of all elements |
| `argmax` | `argmax(list lst)` | Index of the maximum value |
| `argmin` | `argmin(list lst)` | Index of the minimum value |
| `dot` | `dot(any a, any b)` | Dot product of two lists |
| `norm` | `norm(list lst)` | L2 (Euclidean) norm |

**NumPy-backed — list-returning**

These return a **list of strings**. Use `floatOf(listGet(lst, i))` to convert elements to float.

| Function | Signature | Description |
|----------|-----------|-------------|
| `linspace` | `linspace(float start, float stop, int n)` | `n` evenly-spaced values in `[start, stop]` |
| `cumsum` | `cumsum(list lst)` | Cumulative sum |
| `diff` | `diff(list lst)` | Differences between consecutive elements |
| `clip` | `clip(list lst, float lo, float hi)` | Element-wise clamp to `[lo, hi]` |
| `normalize` | `normalize(list lst)` | Scale list to unit L2 length |

---

## typing

String manipulation, type conversion, list utilities, and tuple extras.

> **Tuple core built-ins** (`tupleCreate`, `tupleGet`, `tupleLen`, `tupleContains`, `tupleIndex`, `tupleSlice`, `tupleToList`, `listToTuple`, `tupleConcat`, `tupleCount`, `tupleFirst`, `tupleLast`, `tupleJsonArray`) are always available without importing anything. See [docs/tuples.md](tuples.md) for the full reference.
> This module provides additional higher-level utilities: `tupleReverse`, `tupleSort`, `tupleSortDesc`, `tupleMin`, `tupleMax`, `tupleSum`, `tupleAny`, `tupleAll`, `tupleUnique`, `tupleMean`, `tupleFlatten`, `tupleZip`, `tupleJoin`.

String manipulation, type conversion, and list utilities.

| Function | Signature | Description |
|----------|-----------|-------------|
| `toStr` | `toStr(int n)` | Convert number to string |
| `toInt` | `toInt(str s)` | Parse string as integer (returns `0` on error) |
| `toFloat` | `toFloat(str s)` | Parse string as float (returns `0.0` on error) |
| `toBool` | `toBool(int n)` | `true` if `n != 0` |
| `isNumeric` | `isNumeric(str s)` | `true` if `s` can be parsed as a number |
| `lenStr` | `lenStr(str s)` | Length of string |
| `repeat` | `repeat(str s, int n)` | Repeat `s` `n` times |
| `contains` | `contains(list_or_tuple value, any needle)` | Built-in membership test for lists and tuples |
| `trim` | `trim(str s)` | Strip leading/trailing whitespace |
| `upper` | `upper(str s)` | Convert to uppercase |
| `lower` | `lower(str s)` | Convert to lowercase |
| `startsWith` | `startsWith(str s, str prefix)` | `true` if `s` starts with `prefix` |
| `endsWith` | `endsWith(str s, str suffix)` | `true` if `s` ends with `suffix` |
| `replace` | `replace(str s, str old, str new)` | Replace all occurrences |
| `splitToList` | `splitToList(str s, str sep)` | Split string by `sep` into a list |
| `isList` | `isList(any val)` | `true` if `val` is a list |
| `lenList` | `lenList(list lst)` | Number of elements in a list |
| `flatten` | `flatten(list lst)` | Flatten one level of nested lists |
| `unique` | `unique(list lst)` | Remove duplicates (order preserved) |

---

## fileIO

File system read/write operations.

| Function | Signature | Description |
|----------|-----------|-------------|
| `readFile` | `readFile(str path)` | Read entire file; returns `""` on error |
| `writeFile` | `writeFile(str path, str content)` | Write file (overwrite); returns `bool` |
| `appendFile` | `appendFile(str path, str content)` | Append to file; returns `bool` |
| `fileExists` | `fileExists(str path)` | `true` if file exists |
| `deleteFile` | `deleteFile(str path)` | Delete file; returns `true` on success |
| `readLines` | `readLines(str path)` | Read file as a list of lines |
| `copyFile` | `copyFile(str src, str dst)` | Copy file; returns `bool` |
| `moveFile` | `moveFile(str src, str dst)` | Move/rename file; returns `bool` |
| `fileSize` | `fileSize(str path)` | Size in bytes; returns `-1` on error |
| `tempFile` | `tempFile(str content)` | Write `content` to a temp file; returns path |

---

## shell

External process and command-line execution.

| Function | Signature | Description |
|----------|-----------|-------------|
| `runShell` | `runShell(str cmd)` | Run command, inherit stdio; returns exit code |
| `runShellCapture` | `runShellCapture(str cmd)` | Run command, capture stdout; returns output string |
| `runShellSilent` | `runShellSilent(str cmd)` | Run command (suppress output); returns exit code |
| `runShellErr` | `runShellErr(str cmd)` | Capture stderr only; returns stderr string |
| `runShellCode` | `runShellCode(str cmd)` | Run silently; returns exit code |
| `commandExists` | `commandExists(str cmd)` | `true` if `cmd` is on the system `PATH` |

---

## os

Directory navigation and path utilities wrapping Python's `os` / `os.path`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `getcwd` | `getcwd()` | Current working directory |
| `chdir` | `chdir(str path)` | Change working directory; returns `bool` |
| `listdir` | `listdir(str path)` | Directory entries as a list |
| `mkdir` | `mkdir(str path)` | Create directory; returns `bool` |
| `makedirs` | `makedirs(str path)` | Create directory tree; returns `bool` |
| `rmdir` | `rmdir(str path)` | Remove empty directory; returns `bool` |
| `remove` | `remove(str path)` | Delete a file; returns `bool` |
| `rename` | `rename(str src, str dst)` | Rename/move; returns `bool` |
| `exists` | `exists(str path)` | `true` if path exists |
| `isFile` | `isFile(str path)` | `true` if path is a regular file |
| `isDir` | `isDir(str path)` | `true` if path is a directory |
| `joinPath` | `joinPath(str a, str b)` | Join two path segments |
| `basename` | `basename(str path)` | Final component of path |
| `dirname` | `dirname(str path)` | Parent directory of path |
| `absPath` | `absPath(str path)` | Absolute path |
| `getenv` | `getenv(str name)` | Read environment variable (returns `""` if unset) |
| `getpid` | `getpid()` | Current process ID |
| `sep` | `sep()` | OS path separator (`/` or `\`) |

---

## path

Path manipulation and filesystem helpers wrapping Python's `pathlib.Path`.
Paths are represented as strings; see [the complete path reference](stdlib/path.md).

| Function group | Examples |
|----------------|----------|
| Construction | `cwd`, `home`, `absolute`, `resolve`, `expandUser`, `join`, `normalize` |
| Components | `name`, `stem`, `suffix`, `suffixes`, `parent`, `parts`, `withName`, `withSuffix` |
| Queries | `exists`, `isFile`, `isDir`, `isSymlink`, `isAbsolute`, `sameFile`, `size` |
| Traversal | `iterDir`, `glob`, `rglob` |
| Filesystem | `mkdir`, `mkdirs`, `rmdir`, `unlink`, `touch`, `rename`, `replace` |
| Text I/O | `readText`, `writeText`, `appendText`, `readTextEncoding`, `writeTextEncoding` |

---

## json

JSON encode / decode via Python's `json` module.

| Function | Signature | Description |
|----------|-----------|-------------|
| `jsonValid` | `jsonValid(str s)` | `true` if `s` is valid JSON |
| `jsonParse` | `jsonParse(str s)` | Pretty-print JSON (2-space indent); returns `""` on error |
| `jsonGet` | `jsonGet(str s, str key)` | Get string value at key |
| `jsonGetInt` | `jsonGetInt(str s, str key)` | Get integer value at key |
| `jsonGetFloat` | `jsonGetFloat(str s, str key)` | Get float value at key |
| `jsonGetBool` | `jsonGetBool(str s, str key)` | Get bool value at key |
| `jsonKeys` | `jsonKeys(str s)` | Top-level keys as comma-separated string |
| `jsonStringify` | `jsonStringify(str s)` | JSON-encode a raw string value |
| `jsonArray` | `jsonArray(list lst)` | Serialize a Lynxer list to a JSON array string |
| `jsonObject` | `jsonObject(list lst)` | Build JSON object from flat alternating key/value list |
| `jsonHas` | `jsonHas(str s, str key)` | `true` if key exists in JSON object |
| `jsonLength` | `jsonLength(str s)` | Number of keys in JSON object |
| `jsonSet` | `jsonSet(str s, str key, str val)` | Set a string key; returns new JSON string |
| `jsonDelete` | `jsonDelete(str s, str key)` | Remove a key; returns new JSON string |
| `jsonMerge` | `jsonMerge(str a, str b)` | Merge two JSON objects (`b` wins on conflict) |

---

## js

Run JavaScript via Node.js (Node must be on the system `PATH`).

| Function | Signature | Description |
|----------|-----------|-------------|
| `nodeExists` | `nodeExists()` | Returns `1` if `node` is on PATH, else `0` |
| `nodeVersion` | `nodeVersion()` | Returns Node version string |
| `evalJS` | `evalJS(str expr)` | Evaluate a JS expression; returns result as string |
| `runJS` | `runJS(str script)` | Run a multi-line JS script; returns stdout |
| `runJSFile` | `runJSFile(str path)` | Run a `.js` file; returns stdout |

---

## lua

Run Lua code through the embedded `lupa` runtime. Lua itself does not need to
be installed on the system.

| Function | Signature | Description |
|----------|-----------|-------------|
| `luaExists` | `luaExists()` | Returns `1` if `lupa` is importable, else `0` |
| `luaVersion` | `luaVersion()` | Returns the embedded Lua version string |
| `evalLua` | `evalLua(str expr)` | Evaluate a Lua expression; returns its result as a string |
| `runLua` | `runLua(str script)` | Run a Lua script; returns captured `print` output |
| `runLuaFile` | `runLuaFile(str path)` | Run a `.lua` file; returns captured `print` output |

Each call uses a fresh Lua runtime. Errors are returned as strings beginning
with `"Error: "`.

---

## tui

Terminal UI rendering and input helpers powered by the `rich` Python package.

| Function | Signature | Description |
|----------|-----------|-------------|
| `tuiExists` | `tuiExists()` | Returns `1` if Rich is importable, else `0` |
| `tuiVersion` | `tuiVersion()` | Returns the installed Rich version |
| `init` | `init(str colorSystem)` | Configure the shared Rich console |
| `setWidth` | `setWidth(int width)` | Set console width |
| `setMarkup / setEmoji / setHighlight / setSoftWrap` | `set...(bool enabled)` | Configure console rendering |
| `consoleLog` | `consoleLog(str text)` | Emit a timestamped Rich log line |
| `consoleSaveText / consoleSaveHtml` | `consoleSave...(str path)` | Save recorded console output |
| `printText` | `printText(str text)` | Print text with Rich markup |
| `printStyled` | `printStyled(str text, str style)` | Print literal text with a Rich style |
| `printTextStyle / printTextPlain` | `print...(str text, ...)` | Render Rich `Text` or literal text |
| `markdown` | `markdown(str text)` | Render Markdown |
| `panel` | `panel(str text, str title)` | Render a bordered panel |
| `panelStyled` | `panelStyled(str text, str title, str borderStyle, str contentStyle)` | Render a styled panel |
| `rule` | `rule(str title)` | Render a horizontal rule |
| `ruleStyled` | `ruleStyled(str title, str style)` | Render a styled horizontal rule |
| `jsonPretty` | `jsonPretty(str jsonText)` | Render syntax-highlighted JSON |
| `printSyntax` | `printSyntax(str code, str lexer, bool lineNumbers)` | Syntax-highlight source code |
| `printPretty` | `printPretty(str value)` | Render a Rich Pretty value |
| `printColumns` | `printColumns(str itemsJson, bool equal, bool expand)` | Render items in columns |
| `printAligned / printPadded` | `print...(str text, ...)` | Render aligned or padded content |
| `markupEscape / styleValid` | `...(str value)` | Escape markup or validate styles |
| `table` | `table(str title, str columnsJson, str rowsJson)` | Render a table from JSON arrays |
| `tableCreate / tableAddColumn / tableAddRow / tableSetBox / tablePrint` | `table...` | Stateful table builder using integer handles |
| `treeCreate / treeAdd / treePrint` | `tree...` | Stateful tree builder using integer handles |
| `layoutCreate / layoutSplitRows / layoutSplitColumns / layoutUpdate / layoutPrint` | `layout...` | Stateful Rich layouts |
| `progressStart / progressAddTask / progressAdvance / progressUpdate / progressStop` | `progress...` | Rich progress bars using integer handles |
| `statusStart / statusUpdate / statusStop` | `status...` | Spinner status displays |
| `liveStart / liveUpdate / livePanel / liveStop` | `live...` | Live-updating terminal displays |
| `clear` | `clear()` | Clear the terminal |
| `ask / askDefault / askPassword / askInt / askFloat` | `ask...` | Rich prompt variants |
| `confirm / confirmDefault` | `confirm...` | Rich yes/no prompts |
| `installTraceback / printException` | `...` | Rich traceback integration |

Rendering functions write directly to the terminal. Errors are printed as
`"Error: ..."` messages.

---

## server

Full-featured HTTP server built on **Flask**. See [docs/stdlib/server.md](stdlib/server.md) for the complete reference.

Requires: `pip install flask`

**Key functions:**

| Function | Description |
|----------|-------------|
| `init(host, port)` | Create Flask app bound to `host:port` |
| `setDebug(bool)` | Enable Flask debug mode |
| `setTemplateFolder(path)` | Set Jinja2 template directory |
| `get(path, response)` | Register a `GET` route returning HTML/text |
| `post(path, response)` | Register a `POST` route |
| `put / delete / patch / any` | Other HTTP method routes |
| `getStatus(path, response, status)` | Route with custom HTTP status code |
| `jsonGet(path, jsonStr)` | `GET` route returning JSON |
| `jsonPost / jsonRoute / jsonStatus` | Other JSON routes |
| `template(path, file, dataJson)` | `GET` route rendering a Jinja2 template file |
| `templateString(path, tpl, dataJson)` | `GET` route with an inline Jinja2 template |
| `staticFiles(urlPrefix, dir)` | Serve files from a directory under a URL prefix |
| `staticSite(dir)` | Host a complete static site from a directory |
| `serveFile(path, filepath)` | Serve a single file |
| `redirect(path, target)` | Temporary (302) redirect |
| `redirect301(path, target)` | Permanent (301) redirect |
| `notFound / serverError / forbidden` | Custom error pages |
| `cors()` | Allow all CORS origins |
| `corsOrigin(origin)` | Allow a specific CORS origin |
| `addGlobalHeader(key, val)` | Add a header to every response |
| `enableRequestLog()` | Log METHOD /path before each request |
| `getArg / getForm / getBody / getHeader / getMethod / getUrl / getRemoteAddr` | Request context readers |
| `run()` | Start the server (blocking) |
| `runHTTPS(cert, key)` | Start with SSL |

---

## sys

Runtime information and process control wrapping Python's `sys` module. See [docs/stdlib/sys.md](stdlib/sys.md) for the complete reference.

| Function | Description |
|----------|-------------|
| `version()` | Python version string |
| `versionInfo()` | Version info as JSON object |
| `platform()` | Platform: `"linux"`, `"darwin"`, `"win32"` |
| `executable()` | Path to the Python interpreter |
| `argv()` | Command-line args as JSON array |
| `getArg(index)` | `sys.argv[index]` or `""` |
| `argCount()` | Number of arguments |
| `exit(code)` | Exit process with code |
| `exitOk() / exitError()` | Exit with 0 or 1 |
| `getPath()` | `sys.path` as JSON array |
| `addPath / prependPath / removeFromPath` | Modify `sys.path` |
| `getModules()` | Loaded Python modules as JSON array |
| `isModuleLoaded(name)` | `true` if module is already imported |
| `getRecursionLimit()` | Current recursion limit |
| `setRecursionLimit(n)` | Set recursion limit |
| `getMaxSize()` | `sys.maxsize` |
| `getByteOrder()` | `"little"` or `"big"` |
| `getDefaultEncoding()` | Default string encoding |
| `isFrozen()` | `true` inside a PyInstaller bundle |
| `isatty()` | `true` if stdout is a TTY |

---

## re

Regular expression operations wrapping Python's `re` module. See [docs/stdlib/re.md](stdlib/re.md) for the complete reference.

Pattern strings use Python regex syntax. Multi-match results are returned as JSON arrays.

| Function | Description |
|----------|-------------|
| `test(pattern, string)` | `true` if pattern matches anywhere |
| `match(pattern, string)` | Match at start; returns matched string or `""` |
| `matchFull(pattern, string)` | Match entire string; returns matched string or `""` |
| `search(pattern, string)` | First match anywhere; returns matched string or `""` |
| `findall(pattern, string)` | JSON array of all matches |
| `count(pattern, string)` | Number of non-overlapping matches |
| `groups(pattern, string)` | JSON array of capture groups from first match |
| `groupsAll(pattern, string)` | JSON array-of-arrays of groups for all matches |
| `named(pattern, string)` | JSON object of named capture groups |
| `sub(pattern, repl, string)` | Replace all matches |
| `subN(pattern, repl, string, n)` | Replace first `n` matches |
| `subn(pattern, repl, string)` | Replace all; return JSON `{"result":…,"count":N}` |
| `split(pattern, string)` | Split by pattern; return JSON array |
| `splitN(pattern, string, maxSplit)` | Split at most `maxSplit` times |
| `escape(string)` | Escape regex special characters |
| `matchStart / matchEnd` | Start/end index of first match or `-1` |
| `findSpans(pattern, string)` | JSON array of `{start,end,match}` for all matches |
| `testIgnoreCase / matchIgnoreCase / searchIgnoreCase / findallIgnoreCase / subIgnoreCase` | Case-insensitive variants |
| `findallMultiline / subMultiline / searchDotall` | MULTILINE / DOTALL variants |

---

## colorlib

Terminal colour and text-style helpers using ANSI escape sequences. Written entirely in Lynxer — no Python dependencies.

See [docs/stdlib/colorlib.md](stdlib/colorlib.md) for the complete reference.

```c
global setup(){
    import("colorlib");
}

global main(){
    print(global.colorlib.red("Error!")); print("\n");
    print(global.colorlib.green("OK")); print("\n");
    print(global.colorlib.bold("Important")); print("\n");
}
```

| Category | Functions |
|----------|-----------|
| Foreground | `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white` |
| Bright foreground | `brightBlack`, `brightRed`, `brightGreen`, `brightYellow`, `brightBlue`, `brightMagenta`, `brightCyan`, `brightWhite` |
| Background | `bgBlack`, `bgRed`, `bgGreen`, `bgYellow`, `bgBlue`, `bgMagenta`, `bgCyan`, `bgWhite` |
| Text styles | `bold`, `dim`, `italic`, `underline`, `blink`, `inverse`, `strike` |
| Semantic | `error`, `success`, `warn`, `info`, `heading` |
| Low-level | `reset`, `ansi(text, code)`, `clearScreen`, `cursorHome` |

---

## csv

CSV parsing, writing, filtering, and transformation. See [docs/stdlib/csv.md](stdlib/csv.md) for the complete reference.

```c
global setup(){
    import("csv");
    import("json");
}

global main(){
    str rows = global.csv.readCSV("data.csv");          // JSON array of objects
    print(global.csv.csvRowCount(rows)); print("\n");   // number of data rows
    str first = global.csv.csvRow(rows, 0);             // first row as JSON object
    print(global.json.jsonGet(first, "name")); print("\n");

    str cols = global.csv.csvColumn(rows, "score");     // all values in "score" column
    str result = global.csv.writeCSV("out.csv", rows, "name,score");  // write back
}
```

| Function | Description |
|----------|-------------|
| `readCSV(path)` | Read CSV; returns JSON array of objects (first row = headers) |
| `parseCSV(str)` | Parse a CSV string into JSON array of objects |
| `csvRow(str, n)` | Row `n` (0-based) as a JSON object |
| `csvRowCount(str)` | Number of data rows |
| `csvHeaders(str)` | Comma-separated header names |
| `csvColumn(str, col)` | All values for column `col` as JSON array |
| `writeCSV(path, jsonRows, headers)` | Write CSV file; returns `"ok"` or `"ERROR: ..."` |
| `buildCSV(jsonRows, headers)` | Build CSV string (no file write) |
| `appendRow(csvStr, jsonRow)` | Append a row (JSON array) to a CSV string |
| `filterCSV(str, col, value)` | Keep rows where `col == value` |
| `sortCSV(str, col)` | Sort rows by column (ascending) |
| `dedupCSV(str, col)` | Remove duplicate rows by column |
| `fromTSV(str)` | Convert TSV to CSV |

---

## debug

Runtime inspection, assertions, structured logging, and timers. See [docs/stdlib/debug.md](stdlib/debug.md) for the complete reference.

```c
global setup(){
    import("debug");
}

global main(){
    // assertions
    global.debug.assert(1 > 0, "math is broken");
    global.debug.assertEq("hello", "hello", "mismatch");

    // type inspection
    int x = 42;
    print(global.debug.typeOf(x)); print("\n");    // "int"
    global.debug.dump(x);                          // [debug.dump] type=int  value=42

    // logging
    global.debug.info("starting");
    global.debug.warn("low memory");
    global.debug.error("failed");

    // timers
    global.debug.startTimer("work");
    for(int i = 0; i < 100000; i = i + 1){}
    float ms = global.debug.stopTimer("work");
    print("took "); print(ms); print(" ms\n");
}
```

| Category | Functions |
|----------|-----------|
| Assertions | `assert`, `assertEq`, `assertNotEq`, `assertGt`, `assertLt`, `assertContains` |
| Inspection | `typeOf`, `dump`, `inspect`, `pp` |
| Logging | `log`, `info`, `warn`, `error`, `debug` |
| Timers | `startTimer`, `stopTimer`, `elapsed`, `clock` |
| Environment | `envGet`, `envAll`, `getMemory` |

---

## http

Simple HTTP client built on Python's `urllib` — no extra dependencies. See [docs/stdlib/http.md](stdlib/http.md) for the complete reference.

```c
global setup(){
    import("http");
    import("json");
}

global main(){
    str body = global.http.get("https://api.github.com");
    int status = global.http.getStatus("https://example.com");
    print(status); print("\n");   // 200

    str resp = global.http.postJson("https://httpbin.org/post",
                                    "{\"key\":\"value\"}");
    print(resp); print("\n");
}
```

| Function | Description |
|----------|-------------|
| `get(url)` | GET request; returns body or `"ERROR: ..."` |
| `getStatus(url)` | HTTP status code or `-1` |
| `getHeaders(url)` | Response headers as newline-separated string |
| `post(url, body, contentType)` | POST request |
| `put(url, body, contentType)` | PUT request |
| `delete(url)` | DELETE request |
| `patch(url, body, contentType)` | PATCH request |
| `getJson(url)` | GET with `Accept: application/json` |
| `postJson(url, jsonBody)` | POST with `Content-Type: application/json` |
| `download(url, filepath)` | Write response to file; returns `"ok"` or `"ERROR: ..."` |
| `urlencode(text)` | URL-encode a string |

---

## net

WebSocket client, TCP client, hostname/IP lookup, and URL parsing. See [docs/stdlib/net.md](stdlib/net.md) for the complete reference.

**Requires:** `pip install websockets`

```c
global setup(){
    import("net");
}

global main(){
    // check reachability
    if(global.net.ping("example.com")){
        print("reachable\n");
    }

    // URL parsing
    str parsed = global.net.urlParse("https://api.example.com/v1/data?page=1");
    print(global.net.urlHost("https://api.example.com/v1")); print("\n");  // api.example.com

    // WebSocket round-trip
    global.net.wsConnect("echo", "wss://echo.websocket.org");
    str reply = global.net.wsSendReceive("echo", "Hello!");
    print(reply); print("\n");
    global.net.wsClose("echo");

    // TCP
    global.net.tcpConnect("srv", "example.com", 80);
    global.net.tcpSend("srv", "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n");
    str resp = global.net.tcpReceive("srv", 1024);
    print(resp); print("\n");
    global.net.tcpClose("srv");
}
```

| Category | Functions |
|----------|-----------|
| WebSocket | `wsConnect`, `wsSend`, `wsReceive`, `wsSendReceive`, `wsClose`, `wsConnected` |
| TCP | `tcpConnect`, `tcpSend`, `tcpReceive`, `tcpSendReceive`, `tcpClose` |
| Hostname/IP | `getHostname`, `getLocalIP`, `resolveHost`, `isPortOpen` |
| URL | `urlScheme`, `urlHost`, `urlPath`, `urlParse` |
| HTTP util | `httpHead`, `ping` |

---

## random

Random number and sequence utilities wrapping Python's `random`. See [docs/stdlib/random.md](stdlib/random.md) for the complete reference.

```c
global setup(){
    import("random");
}

global main(){
    global.random.seed(0);                      // 0 = system entropy

    float r = global.random.random();           // [0.0, 1.0)
    int n = global.random.randint(1, 10);       // integer in [1,10]
    float u = global.random.uniform(1.0, 5.0);  // float in [1.0, 5.0]
    bool flip = global.random.coinflip();        // 50/50 bool

    str uid = global.random.uuid4();             // "f47ac10b-58cc-..."
    str picked = global.random.sampleStr("cat|dog|fish");   // one of the three
    str shuffled = global.random.shuffle("a|b|c|d");         // e.g. "c|a|d|b"
}
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `seed` | `seed(int n)` | Seed RNG; pass `0` for system entropy |
| `random` | `random()` | Float in `[0.0, 1.0)` |
| `randint` | `randint(int a, int b)` | Integer in `[a, b]` inclusive |
| `uniform` | `uniform(float a, float b)` | Float in `[a, b]` |
| `randrange` | `randrange(int start, int stop)` | Integer in `[start, stop)` |
| `randrangeStep` | `randrangeStep(int start, int stop, int step)` | Ranged integer with step |
| `gauss` | `gauss(float mu, float sigma)` | Gaussian random float |
| `coinflip` | `coinflip()` | `true` or `false` equally |
| `sampleInt` | `sampleInt(str items)` | Pick one int from pipe-separated list |
| `sampleStr` | `sampleStr(str items)` | Pick one string from pipe-separated list |
| `shuffle` | `shuffle(str items)` | Shuffle pipe-separated list; return new string |
| `triangular` | `triangular(float lo, float hi, float mid)` | Triangular distribution |
| `uuid4` | `uuid4()` | Random UUID4 string |
| `randHex` | `randHex(int n)` | `n` hex characters |

---

## time

Date and time utilities wrapping Python's `datetime`. See [docs/stdlib/time.md](stdlib/time.md) for the complete reference.

```c
global setup(){
    import("time");
}

global main(){
    print(global.time.now()); print("\n");       // "2024-08-01 14:30:00"
    print(global.time.getDate()); print("\n");   // "2024-08-01"
    print(global.time.getYear()); print("\n");   // 2024
    print(global.time.timestamp()); print("\n"); // 1722520200.0

    // arithmetic
    str tomorrow = global.time.addDays("2024-08-01", 1);
    print(tomorrow); print("\n");                // "2024-08-02"

    int diff = global.time.diffDays("2024-08-01", "2024-09-01");
    print(diff); print("\n");                    // 31
}
```

| Function | Description |
|----------|-------------|
| `now()` | Current datetime as `"YYYY-MM-DD HH:MM:SS"` |
| `getTime()` | Current time as `"HH:MM:SS"` |
| `getDate()` | Current date as `"YYYY-MM-DD"` |
| `getYear / getMonth / getDay / getHour / getMinute / getSecond` | Numeric components |
| `getWeekday / getWeekdayNum` | Day name and 0-based number |
| `isoNow()` | ISO 8601 datetime string |
| `format(pattern)` | Format current datetime with `strftime` pattern |
| `timestamp()` | Unix epoch timestamp (float) |
| `fromTimestamp(ts)` | Epoch → `"YYYY-MM-DD HH:MM:SS"` |
| `toTimestamp(dt)` | `"YYYY-MM-DD HH:MM:SS"` → epoch (`-1.0` on error) |
| `addDays(date, n)` | Add `n` days to a `"YYYY-MM-DD"` string |
| `diffDays(d1, d2)` | Days between two `"YYYY-MM-DD"` strings |
| `isLeapYear(year)` | `true` if `year` is a leap year |
| `daysInMonth(year, month)` | Number of days in a month |

---

## regex

Extended regular expressions with compiled pattern caching. See [docs/stdlib/regex.md](stdlib/regex.md) for the complete reference.

Uses the `regex` package when installed (`pip install regex`), falling back to `re`.

> For basic one-off regex work, use `re` instead. Import `regex` when you need a compiled pattern cache, advanced Unicode support (`\p{L}`), or the extended helpers.

```c
global setup(){
    import("regex");
}

global main(){
    // compile once, reuse many times
    global.regex.compile("email",
        "[a-zA-Z0-9._%+\\-]+@[a-zA-Z0-9.\\-]+\\.[a-zA-Z]{2,}", "");

    print(global.regex.testCompiled("email", "user@example.com")); print("\n");  // true
    str all = global.regex.findallCompiled("email", "a@b.com and c@d.org");
    print(all); print("\n");   // ["a@b.com","c@d.org"]

    // replace only the 2nd match
    str r = global.regex.replaceNth("cat", "dog", "cat cat cat", 2);
    print(r); print("\n");   // cat dog cat
}
```

| Category | Functions |
|----------|-----------|
| Cache | `compile`, `testCompiled`, `matchCompiled`, `findallCompiled`, `subCompiled`, `clearCache` |
| Validation | `isValid` |
| Extraction | `extract`, `extractAll`, `unique` |
| Replace | `replaceNth`, `replaceAllLiteral` |
| Search | `lastMatch`, `highlight`, `splitKeep` |
| Utility | `globToRegex`, `countWords`, `truncateMatch` |

---

## tkinter

Comprehensive GUI toolkit (standard + CTk extension). See [docs/stdlib/tkinter.md](stdlib/tkinter.md) for the complete reference.

Requires a desktop display. Widgets are referenced by integer indexes.

### Standard tkinter

**Workflow:** `init` → create widgets → optionally style/layout → `run`.

**Key widget creators (all return index):** `label`, `labelStyled`, `button`, `buttonStyled`, `entry`, `passwordEntry`, `textBox`, `checkbox`, `radioButton`, `listbox`, `scale`, `scaleVertical`, `spinbox`, `progressBar`, `combobox`, `notebook`, `canvas`, `frame`, `labelFrame`, `separator`, `scrollbar`, `image`

**Key operations:** `getValue`, `setText`, `setEntryText`, `isChecked`, `setChecked`, `getRadio`, `getListSelection`, `getScale`, `setScale`, `getCombo`, `setProgress`, `disableWidget`, `enableWidget`, `hideWidget`, `showWidget`, `clearWidget`, `focusWidget`

**Styling:** `setFont`, `setForeground`, `setBackgroundWidget`, `setPadding`, `setCursor`, `setBorder`

**Layout:** `grid`, `gridSpan`, `place`, `pack`

**Menus:** `menubar`, `addMenu`, `addMenuItem`, `addMenuSeparator`, `addCheckMenuItem`, `contextMenu`, `addContextItem`, `showContext`

**Canvas:** `canvasLine`, `canvasRect`, `canvasOval`, `canvasText`, `canvasPolygon`, `canvasClear`

**Dialogs:** `messageBox`, `warningBox`, `errorBox`, `askYesNo`, `askOkCancel`, `openFileDialog`, `openFileDialogFilter`, `saveFileDialog`, `openDirDialog`, `askColor`, `askString`, `askInteger`, `askFloat`

**Window:** `setTitle`, `resize`, `setBackground`, `disableResize`, `setMinSize`, `setMaxSize`, `setOpacity`, `center`, `topmost`, `iconify`, `deiconify`, `update`, `bindClose`

### CustomTkinter (CTk) extension

Built into the same `tkinter` module — just `import("tkinter")`. Requires: `pip install customtkinter`. Do **not** mix `init`/`label`/… and `ctkInit`/`ctkLabel`/… in the same program.

**Workflow:** `ctkSetAppearance` → `ctkSetTheme` → `ctkInit` → create widgets → `ctkRun`.

**Theme:** `ctkSetAppearance(mode)` (`"dark"`, `"light"`, `"system"`), `ctkSetTheme(theme)` (`"blue"`, `"green"`, `"dark-blue"`)

**Key widget creators (all return index):** `ctkLabel`, `ctkLabelStyled`, `ctkButton`, `ctkButtonStyled`, `ctkEntry`, `ctkEntryPlaceholder`, `ctkPasswordEntry`, `ctkTextBox`, `ctkCheckbox`, `ctkSwitch`, `ctkRadioButton`, `ctkCombobox`, `ctkOptionMenu`, `ctkSegmented`, `ctkSlider`, `ctkProgressBar`, `ctkFrame`, `ctkScrollFrame`, `ctkTabView`, `ctkImage`

**Key operations:** `ctkGetValue`, `ctkGetText`, `ctkSetText`, `ctkSetEntryText`, `ctkIsChecked`, `ctkSetChecked`, `ctkIsOn`, `ctkSetSwitch`, `ctkGetRadio`, `ctkGetSlider`, `ctkSetSlider`, `ctkGetCombo`, `ctkSetCombo`, `ctkGetOption`, `ctkSetOption`, `ctkSetProgress`, `ctkDisableWidget`, `ctkEnableWidget`, `ctkHideWidget`, `ctkShowWidget`, `ctkDestroyWidget`, `ctkFocusWidget`

**Styling:** `ctkSetFont`, `ctkSetForeground`, `ctkSetBackgroundWidget`, `ctkSetCornerRadius`, `ctkSetBorderWidth`, `ctkSetPadding`

**Layout:** `ctkGrid`, `ctkGridSpan`, `ctkPlace`, `ctkPack`

**Window:** `ctkSetTitle`, `ctkResize`, `ctkSetBackground`, `ctkDisableResize`, `ctkSetMinSize`, `ctkSetMaxSize`, `ctkSetOpacity`, `ctkCenter`, `ctkTopmost`, `ctkIconify`, `ctkDeiconify`, `ctkUpdate`, `ctkBindClose`

---

## game

2-D game development. See [docs/stdlib/game.md](stdlib/game.md) for the complete reference.

Requires: `pip install arcade`. The draw/update loop is wired via `rawPy` callbacks; see the [game docs](stdlib/game.md) for the pattern.

**Key functions:**

| Category | Functions |
|----------|-----------|
| Window | `init`, `setTitle`, `setBackground`, `getWidth`, `getHeight`, `close` |
| App | `run` |
| Draw loop | `beginDraw`, `endDraw` |
| Shapes | `drawRect`, `drawRectOutline`, `drawCircle`, `drawCircleOutline`, `drawEllipse`, `drawEllipseOutline`, `drawLine`, `drawTriangle`, `drawTriangleOutline`, `drawPolygon`, `drawArc`, `drawPoint` |
| Text | `drawText`, `drawTextStyled` |
| Sprites | `loadSprite`, `setSpritePos`, `setSpriteAngle`, `setSpriteScale`, `setSpriteVelocity`, `getSpriteX`, `getSpriteY`, `getSpriteAngle`, `updateSprite`, `drawSprite`, `spriteCollides` |
| Sprite lists | `makeSpriteList`, `addToList`, `drawSpriteList`, `updateSpriteList` |
| Input | `keyDown`, `keyUp`, `mouseX`, `mouseY`, `mouseLeft`, `mouseRight` |
| Sound | `loadSound`, `playSound`, `stopSound` |
| Timer | `deltaTime` |
| Camera | `makeCamera`, `useCamera`, `setCameraPos`, `resetCamera` |
| Physics | `makePhysicsEngine`, `setPhysicsPlayer`, `updatePhysics`, `canJump` |

---

## turtle

Turtle graphics. See [docs/stdlib/turtle.md](stdlib/turtle.md) for the complete reference.

Requires a desktop display (Tk). Call `done()` at the end of `main()`.

**Key functions:**

| Category | Functions |
|----------|-----------|
| Window | `init`, `title`, `bgcolor`, `bgpic`, `screensize`, `window_width`, `window_height`, `mode`, `colormode`, `tracer`, `update` |
| Movement | `forward`, `backward`, `right`, `left`, `goto`, `setx`, `sety`, `home`, `setheading`, `towards` |
| Pen | `penup`, `pendown`, `pencolor`, `pensize`, `color`, `fillcolor`, `begin_fill`, `end_fill`, `isdown`, `speed` |
| Drawing | `circle`, `arc`, `dot`, `dotColor`, `write`, `writeFont`, `writeAligned` |
| Stamps | `stamp`, `clearstamp`, `undo` |
| Appearance | `shape`, `addshape`, `turtlesize`, `resizemode`, `hideturtle`, `showturtle` |
| State | `xcor`, `ycor`, `heading`, `pos`, `distance` |
| Screen | `clear`, `reset` |
| Events | `listen`, `onkey`, `onclick`, `onscreenclick`, `exitonclick` |
| Dialogs | `numinput`, `textinput` |
| Shapes | `polygon`, `star`, `grid`, `spiral` |
| App | `done` |
