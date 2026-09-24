# Terminal UI Module

The `tui` module provides terminal UI functionality using Rust's `ratatui` and `crossterm` crates.

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
- [parity.md](../parity.md) — the parity scope with Python Lynxer.
- [limitations.md](../limitations.md) — the full divergence register.
