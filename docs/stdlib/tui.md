# tui

Terminal UI helpers powered by the
[Rich](https://rich.readthedocs.io/) Python package.

Rich is installed by the standard project build from `requirements_venv.txt`.
It does not require a separate executable.

Import it inside `setup()`:

```lynx
global setup(){
    import("tui");
}
```

## Core and console

- `tuiExists()` → `1` when Rich is importable, otherwise `0`.
- `tuiVersion()` → installed Rich version, or `""` when unavailable.
- `init(colorSystem)` → replace the shared console; use `""`, `"standard"`,
  `"256"`, or `"truecolor"`.
- `setWidth(width)`, `setMarkup(enabled)`, `setEmoji(enabled)`,
  `setHighlight(enabled)`, `setSoftWrap(enabled)` → configure the console.
- `consoleLog(text)` → emit a timestamped Rich log line.
- `consoleSaveText(path)`, `consoleSaveHtml(path)` → save recorded console
  output; return `"ok"` or `"error:..."`.

## Renderables and formatting

- `printText(text)` → print text with Rich markup support.
- `printStyled(text, style)` → print literal text with a Rich style.
- `printTextStyle(text, style)` → render a styled `Text` object.
- `printTextPlain(text)` → print literal text without markup or highlighting.
- `markdown(text)` → render Markdown in the terminal.
- `panel(text, title)` → render a bordered panel; pass `""` for no title.
- `panelStyled(text, title, borderStyle, contentStyle)` → render a styled panel.
- `rule(title)` → render a horizontal rule with an optional title.
- `ruleStyled(title, style)` → render a styled horizontal rule.
- `jsonPretty(jsonText)` → syntax-highlight a JSON document.
- `printSyntax(code, lexer, lineNumbers)` → syntax-highlight source code with a
  Pygments lexer such as `"python"` or `"javascript"`.
- `printPretty(value)` → render a Rich `Pretty` value.
- `printColumns(itemsJson, equal, expand)` → render a JSON array in columns.
- `printAligned(text, align, pad)` → render with `"left"`, `"center"`, or `"right"` alignment.
- `printPadded(text, top, right, bottom, left)` → render with terminal padding.
- `markupEscape(text)` → escape Rich markup characters.
- `styleValid(style)` → validate a Rich style string.

## Tables, trees, and layouts

- `table(title, columnsJson, rowsJson)` → render a table from JSON arrays.
- `tableCreate(title)` → create a table and return its integer handle.
- `tableAddColumn(handle, header, style)` and `tableAddRow(handle, valuesJson)` →
  add table content.
- `tableSetCaption(handle, caption)`, `tableSetHeader(handle, enabled)`,
  `tableSetLines(handle, enabled)`, `tableSetBox(handle, boxName)`,
  `tableSetExpand(handle, enabled)`, `tablePrint(handle)`.
- `treeCreate(label)` → create a tree and return its handle.
- `treeAdd(parentHandle, label)` → add a child and return its handle.
- `treePrint(handle)` → render a tree.
- `layoutCreate(name)` → create a layout and return its handle.
- `layoutSplitRows(handle, namesJson)` and `layoutSplitColumns(handle, namesJson)`.
- `layoutUpdate(handle, name, text)`, `layoutPanel(handle, name, text, title)`,
  `layoutPrint(handle)`.

Handles are integer indexes, following the same pattern as `tkinter.lynx`.

## Progress and live displays

- `progressStart()` → start a Rich progress display and return its handle.
- `progressAddTask(handle, description, total)` → return a task handle.
- `progressAdvance(handle, task, amount)`,
  `progressUpdate(handle, task, completed, total)`,
  `progressStop(handle)`.
- `statusStart(text)` → start a spinner and return its handle.
- `statusUpdate(handle, text)`, `statusStop(handle)`.
- `liveStart(text, refreshPerSecond)` → start a live display and return its handle.
- `liveUpdate(handle, text)`, `livePanel(handle, text, title)`,
  `liveStop(handle)`.

## Prompts and tracebacks

- `clear()` → clear the terminal.
- `ask(prompt)` → read a line from the user.
- `askDefault(prompt, defaultValue)` → prompt with a default.
- `askPassword(prompt)`, `askInt(prompt)`, `askFloat(prompt)`.
- `confirm(prompt)` → read a yes/no answer and return `1` or `0`.
- `confirmDefault(prompt, defaultValue)`.
- `installTraceback(showLocals)` → install Rich's traceback hook.
- `printException()` → print the active Python exception with Rich formatting.

Example:

```lynx
global setup(){
    import("tui");
}

global main(){
    global.tui.printStyled("Lynxer is ready", "bold green");
    global.tui.rule("Status");
    global.tui.panel("Rich terminal output from Lynxer.", "tui");
    global.tui.table(
        "Users",
        "[\"Name\", \"Role\"]",
        "[[\"Ada\", \"admin\"], [\"Linus\", \"user\"]]"
    );
}
```

Rendering functions write directly to the terminal. If Rich is unavailable,
they print an `Error: ...` message instead of raising a Lynxer runtime error.
Progress, status, live, table, tree, and layout handles remain valid until the
Lynxer process exits or `init()` resets them.