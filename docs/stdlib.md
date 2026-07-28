# Standard Library

Lynxer ships a set of standard library modules in the `stdlib/` folder. Import any of them inside `setup()`:

```c
global setup(){
    import("math");
    import("typing");
    import("fileIO");
    import("shell");
    import("os");
    import("json");
    import("js");
    import("server");
    import("sys");
    import("re");
    import("tkinter");
    import("turtle");
}
```

All functions are accessed via `global.<module>.<function>(...)`.

See the [stdlib/ documentation folder](stdlib/README.md) for individual module pages.

---

## math

Mathematical operations wrapping Python's `math` and `random` modules.

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs` | `abs(float n)` | Absolute value |
| `max` | `max(float a, float b)` | Larger of two values |
| `min` | `min(float a, float b)` | Smaller of two values |
| `clamp` | `clamp(float val, float lo, float hi)` | Clamp `val` to `[lo, hi]` |
| `pow` | `pow(float base, float exp)` | `base ^ exp` |
| `sqrt` | `sqrt(float n)` | Square root |
| `floor` | `floor(float n)` | Round down to nearest integer |
| `ceil` | `ceil(float n)` | Round up to nearest integer |
| `round` | `round(float n)` | Round to nearest integer |
| `pi` | `pi()` | Returns `3.141592653589793` |
| `e` | `e()` | Returns `2.718281828459045` |
| `log` | `log(float n)` | Natural logarithm (returns `0.0` if n ≤ 0) |
| `log2` | `log2(float n)` | Base-2 logarithm |
| `log10` | `log10(float n)` | Base-10 logarithm |
| `sin` | `sin(float n)` | Sine (radians) |
| `cos` | `cos(float n)` | Cosine (radians) |
| `tan` | `tan(float n)` | Tangent (radians) |
| `degrees` | `degrees(float n)` | Radians → degrees |
| `radians` | `radians(float n)` | Degrees → radians |
| `sign` | `sign(float n)` | `-1`, `0`, or `1` |
| `isEven` | `isEven(int n)` | `true` if `n` is even |
| `isOdd` | `isOdd(int n)` | `true` if `n` is odd |
| `factorial` | `factorial(int n)` | `n!` (returns `1` for n ≤ 0) |
| `gcd` | `gcd(int a, int b)` | Greatest common divisor |
| `hypot` | `hypot(float a, float b)` | `sqrt(a² + b²)` |
| `truncate` | `truncate(float n)` | Remove fractional part (toward zero) |
| `randInt` | `randInt(int lo, int hi)` | Random integer in `[lo, hi]` inclusive |
| `randFloat` | `randFloat(float lo, float hi)` | Random float in `[lo, hi)` |

---

## typing

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
| `contains` | `contains(str haystack, str needle)` | `true` if `needle` is in `haystack` |
| `trim` | `trim(str s)` | Strip leading/trailing whitespace |
| `upper` | `upper(str s)` | Convert to uppercase |
| `lower` | `lower(str s)` | Convert to lowercase |
| `startsWith` | `startsWith(str s, str prefix)` | `true` if `s` starts with `prefix` |
| `endsWith` | `endsWith(str s, str suffix)` | `true` if `s` ends with `suffix` |
| `replace` | `replace(str s, str old, str new)` | Replace all occurrences |
| `toList` | `toList(str s, str sep)` | Split string by `sep` into a list |
| `isList` | `isList(any val)` | `true` if `val` is a list |
| `lenList` | `lenList(any lst)` | Number of elements in a list |
| `flatten` | `flatten(any lst)` | Flatten one level of nested lists |
| `unique` | `unique(any lst)` | Remove duplicates (order preserved) |

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
| `jsonArray` | `jsonArray(any lst)` | Serialize a Lynxer list to a JSON array string |
| `jsonObject` | `jsonObject(any lst)` | Build JSON object from flat alternating key/value list |
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

## tkinter

Comprehensive GUI toolkit. See [docs/stdlib/tkinter.md](stdlib/tkinter.md) for the complete reference.

Requires a desktop display. Widgets are referenced by integer indexes.

**Workflow:** `init` → create widgets → optionally style/layout → `run`.

**Key widget creators (all return index):** `label`, `labelStyled`, `button`, `buttonStyled`, `entry`, `passwordEntry`, `textBox`, `checkbox`, `radioButton`, `listbox`, `scale`, `scaleVertical`, `spinbox`, `progressBar`, `combobox`, `notebook`, `canvas`, `frame`, `labelFrame`, `separator`, `scrollbar`, `image`

**Key operations:** `getValue`, `setText`, `setEntryText`, `isChecked`, `setChecked`, `getRadio`, `getListSelection`, `getScale`, `setScale`, `getCombo`, `setProgress`, `disableWidget`, `enableWidget`, `hideWidget`, `showWidget`, `clearWidget`, `focusWidget`

**Styling:** `setFont`, `setForeground`, `setBackgroundWidget`, `setPadding`, `setCursor`, `setBorder`

**Layout:** `grid`, `gridSpan`, `place`, `pack`

**Menus:** `menubar`, `addMenu`, `addMenuItem`, `addMenuSeparator`, `addCheckMenuItem`, `contextMenu`, `addContextItem`, `showContext`

**Canvas:** `canvasLine`, `canvasRect`, `canvasOval`, `canvasText`, `canvasPolygon`, `canvasClear`

**Dialogs:** `messageBox`, `warningBox`, `errorBox`, `askYesNo`, `askOkCancel`, `openFileDialog`, `openFileDialogFilter`, `saveFileDialog`, `openDirDialog`, `askColor`, `askString`, `askInteger`, `askFloat`

**Window:** `setTitle`, `resize`, `setBackground`, `disableResize`, `setMinSize`, `setMaxSize`, `setOpacity`, `center`, `topmost`, `iconify`, `deiconify`, `update`, `bindClose`

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
