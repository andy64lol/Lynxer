# Terminal UI Module

The `tui` module provides terminal UI functionality using Rust's `ratatui` and `crossterm` crates.

## How it renders

The backend never takes over the terminal for a plain rendering call. Each
rendering function draws a `ratatui` widget into an offscreen buffer and prints
the result:

- **Without a TTY** (piped output, CI, the test suite) the result is plain text
  with box-drawing characters, so the output is deterministic.
- **With a TTY and color enabled** the same text carries ANSI SGR styling.
  `init(colorSystem)` accepts a system name; one containing `none` disables
  color.

`setWidth(width)` pins the render width (default `80`) and `setSoftWrap`
controls wrapping. `panel*`, `rule*`, `table*`, `tree*`, `layout*`, `progress*`,
`status*` and `live*` render the state that was previously stored, so they
reflect the data rather than a fixed template. Progress, status and live
families print a snapshot on each update.

`markdown` renders CommonMark through [`pulldown-cmark`](https://crates.io/crates/pulldown-cmark)
(headings, emphasis, inline and fenced code, ordered/unordered/task lists, block
quotes, rules, links and simple tables), and `printSyntax` highlights code with
[`syntect`](https://crates.io/crates/syntect) using Sublime syntax definitions.
The highlighted syntax and theme sets are embedded in the module, so nothing is
read from disk and no system oniguruma is required (syntect uses the pure-Rust
`fancy-regex` backend). `theme` chooses a highlighting theme, `themeNames`
lists them and `themeName` reports the active one.

## Handles

`table*`, `tree*`, `layout*`, `progress*`, `status*` and `live*` return integer
handles into backend registries. A handle stays valid for the run; progress,
status and live free theirs on `stop`. An invalid handle returns `-1`.

## Prompts

`ask`, `askPassword`, `askInt`, `askFloat`, `askDefault`, `confirm` and
`confirmDefault` read one line from stdin. At end-of-file — a closed pipe, a
redirected file, CI — they return the documented default instead of blocking, so
a script is safe to run non-interactively. `askPassword` disables echo only when
stdin is a terminal. `askInt`/`askFloat` return `0`/`0.0` for unparsable input.

## Styles and markup

`printText` interprets `[tag]…[/tag]` markup when markup is enabled, and
`markupEscape` escapes a literal `[`. A style is a subset of Rich's syntax:
modifiers (`bold`, `dim`, `italic`, `underline`, `blink`, `reverse`, `hidden`,
`strike`) and colors (`red`, `bright blue`, `white on blue`, …). `styleValid`
returns `false` when a string contains an unknown token.

## Functions

### Existence and Version
- `tuiExists() -> int` — Returns `1` if the backend is available.
- `tuiVersion() -> string` — Returns the backend version, or `""`.

### Printing
- `printText(text: string)` — Prints text with markup support.
- `printStyled(text: string, style: string)` — Prints text with a style.
- `markdown(text: string)` — Renders Markdown to the terminal.
- `jsonPretty(jsonText: string)` — Pretty-prints JSON with syntax highlighting.
- `printSyntax(code: string, lexer: string, lineNumbers: bool)` — Syntax-highlight source code.
- `printTextStyle(text: string, style: string)` — Prints text with a style.
- `printTextPlain(text: string)` — Prints plain text.
- `printPretty(value: string)` — Pretty-prints a value.
- `printColumns(itemsJson: string, equal: bool, expand: bool)` — Prints columns.
- `printAligned(text: string, align: string, pad: bool)` — Prints aligned text.
- `printPadded(text: string, top: int, right: int, bottom: int, left: int)` — Prints padded text.
- `printException()` — Prints exception traceback.
- `installTraceback(showLocals: bool)` — Installs traceback.

### Panels and Rules
- `panel(text: string, title: string)` — Renders a bordered panel.
- `panelStyled(text: string, title: string, borderStyle: string, contentStyle: string)` — Panel with styles.
- `rule(title: string)` — Prints a horizontal rule.
- `ruleStyled(title: string, style: string)` — Styled rule.

### Tables
- `table(title: string, columnsJson: string, rowsJson: string)` — Renders a table.
- `tableCreate(title: string) -> int` — Creates a table, returns handle.
- `tableAddColumn(idx: int, header: string, style: string)` — Adds a column.
- `tableAddRow(idx: int, valuesJson: string)` — Adds a row.
- `tableSetCaption(idx: int, caption: string)` — Sets caption.
- `tableSetHeader(idx: int, showHeader: bool)` — Sets header visibility.
- `tableSetLines(idx: int, showLines: bool)` — Sets lines visibility.
- `tableSetBox(idx: int, boxName: string)` — Sets box style.
- `tableSetExpand(idx: int, expand: bool)` — Sets expand.
- `tablePrint(idx: int)` — Prints a table.

### Trees
- `treeCreate(label: string) -> int` — Creates a tree, returns handle.
- `treeAdd(parentIdx: int, label: string) -> int` — Adds a child.
- `treePrint(idx: int)` — Prints a tree.

### Layouts
- `layoutCreate(name: string) -> int` — Creates a layout, returns handle.
- `layoutSplitRows(idx: int, namesJson: string)` — Splits rows.
- `layoutSplitColumns(idx: int, namesJson: string)` — Splits columns.
- `layoutUpdate(idx: int, name: string, text: string)` — Updates a section.
- `layoutPanel(idx: int, name: string, text: string, title: string)` — Sets a panel.
- `layoutPrint(idx: int)` — Prints a layout.

### Progress, Status, Live
- `progressStart() -> int` — Starts a progress bar, returns handle.
- `progressAddTask(idx: int, description: string, total: float) -> int` — Adds a task.
- `progressAdvance(idx: int, taskId: int, amount: float)` — Advances a task.
- `progressUpdate(idx: int, taskId: int, completed: float, total: float)` — Updates a task.
- `progressStop(idx: int)` — Stops a progress bar.
- `statusStart(text: string) -> int` — Starts a status, returns handle.
- `statusUpdate(idx: int, text: string)` — Updates a status.
- `statusStop(idx: int)` — Stops a status.
- `liveStart(text: string, refreshPerSecond: float) -> int` — Starts live display.
- `liveUpdate(idx: int, text: string)` — Updates live display.
- `livePanel(idx: int, text: string, title: string)` — Updates live panel.
- `liveStop(idx: int)` — Stops live display.

### Console Configuration
- `init(colorSystem: string)` — Creates or replaces the console.
- `setWidth(width: int) -> int` — Sets console width.
- `setMarkup(enabled: bool)` — Enables/disables markup.
- `setEmoji(enabled: bool)` — Enables/disables emoji.
- `setHighlight(enabled: bool)` — Enables/disables highlight.
- `setSoftWrap(enabled: bool)` — Enables/disables soft wrap.
- `consoleLog(text: string)` — Logs text.
- `consoleSaveText(path: string) -> string` — Saves console text to file.
- `consoleSaveHtml(path: string) -> string` — Saves console HTML to file.

### Markup and Styles
- `markupEscape(text: string) -> string` — Escapes markup.
- `styleValid(style: string) -> bool` — Validates a style string.

### Prompts
- `ask(prompt: string) -> string` — Reads a line.
- `askPassword(prompt: string) -> string` — Reads a password.
- `askInt(prompt: string) -> int` — Reads an integer.
- `askFloat(prompt: string) -> float` — Reads a float.
- `askDefault(prompt: string, defaultValue: string) -> string` — Reads with default.
- `confirm(prompt: string) -> bool` — Yes/no prompt.
- `confirmDefault(prompt: string, defaultValue: bool) -> bool` — Yes/no with default.

### TUI Mode
- `enter() -> int` — Enters TUI mode.
- `exit() -> int` — Exits TUI mode.
- `clear() -> int` — Clears the terminal screen.

### Widgets
- `list(itemsJson: string, title: string, numbered: bool)` — Bulleted or numbered list.
- `tabs(labelsJson: string, active: int)` — A row of tabs with one highlighted.
- `barChart(title: string, dataJson: string, width: int, height: int)` — Bar chart from numbers.
- `sparkline(dataJson: string)` — Sparkline from numbers.
- `calendar(year: int, month: int)` — A month grid.
- `jsonTree(jsonText: string)` — JSON as an indented tree.
- `gauge(label: string, ratio: float, width: int)` — A filled gauge.
- `lineGauge(label: string, ratio: float, width: int)` — A one-line gauge.
- `tableFromCsv(title: string, csvText: string)` — A table parsed from CSV.

### Terminal Control
- `terminalWidth() -> int` / `terminalHeight() -> int` — Size, or the configured width / a default without a TTY.
- `setCursor(x: int, y: int)` / `moveCursor(dx: int, dy: int)` — Move the cursor (TTY only).
- `hideCursor()` / `showCursor()` — Toggle the cursor (TTY only).
- `bell()` — Ring the terminal bell (TTY only).

### Input
- `input(timeoutMs: int) -> string` — One key, or `""` on timeout / without a TTY.
- `pollInput(timeoutMs: int) -> bool` — Whether a key is waiting.

### Selection Prompts
- `select(prompt: string, choicesJson: string) -> int` — Index of the chosen item, or `-1`.
- `multiselect(prompt: string, choicesJson: string) -> string` — JSON array of chosen indices.
- `editor(prompt: string, defaultText: string) -> string` — Multi-line input until a lone `.` or EOF.

### Screen and Themes
- `screenStart(text: string)` / `screenUpdate(text: string)` / `screenStop()` — Full-screen display; prints a snapshot per update.
- `theme(name: string) -> bool` — Select a highlighting theme.
- `themeNames() -> string` — JSON array of theme names.
- `themeName() -> string` — The active theme.

## Example

```lynx
global setup(){
    import("tui")
}

global main(){
    global.tui.init("truecolor");
    global.tui.printText("Hello, TUI!");
    global.tui.panel("Hello", "Title");
    global.tui.clear();
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
