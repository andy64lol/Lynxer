# IDEs

Lynxer ships two IDEs, both launched from the interpreter itself and both
listed by `--help`:

| Command | IDE | Needs |
|---------|-----|-------|
| `lynxer --cli-ide [file.lynx]` | Built-in terminal IDE: a full-screen ANSI TUI inside the interpreter, no separate binary | An interactive TTY on stdin **and** stdout |
| `lynxer --gui-ide` | Native GUI IDE (iced, no webview): file tree, tabs, autocomplete, Problems/Output panels | A graphical display and the `lynxer-ide` executable |

Both IDEs are clients of the interpreter: Run, Lint and Format shell out to
`lynxer` and show its output in a panel.

## Terminal IDE

`--cli-ide` runs inside the interpreter. It opens in the current directory and
accepts one optional argument: a `.lynx` file to open, or a folder to browse.
It refuses to start when stdin or stdout is redirected, and restores the
terminal on exit (alternate screen, cursor and raw-input mode).

```bash
lynxer --cli-ide              # browse the current directory
lynxer --cli-ide program.lynx # open a file
lynxer --cli-ide src/         # browse a folder
```

### Layout

The layout follows nano: a reverse-video title bar with the version, file name
and `Modified` marker, a plain editor body, a message line, and two rows of
`^X`-style shortcut hints. No function keys are used — every action is a
`Ctrl` combo. `Ctrl+Alt+C` (also `^A` / `^C`) opens a **command menu** that
lists every command with its shortcut; navigate it with the arrow keys and run
the highlighted entry with Enter, or press its shortcut directly. Esc closes
any overlay or prompt.

```text
  LYNXER IDE Lynxer 0.1.9b3        File: program.lynx   Modified
  [program.lynx]
   1 | fn main(){
   2 |   println("Hello, Lynxer!");
   3 | }
 Ln 2, Col 3   Lynxer   UTF-8   modified   0 errors  0 warnings   Ready
 ^G Help   ^O Open   ^S Save   ^X Exit   ^W Close   ^N New
 ^F Find   ^R Run   ^L Lint   ^T Format   ^P Side   ^E Edit   M-^C More
```

### File tree

When no file is open the IDE starts in the file tree: Up/Down move through
files and folders, Right expands a folder and steps into it, Left collapses a
folder or jumps back to its parent, and Enter opens a file in the editor or
expands a folder. Selecting an already-open file or an expanded folder
**closes** it instead; when any buffer has unsaved edits a
`Save all changes? (Y/n/c)` prompt appears first (Y saves every dirty buffer,
N discards, C/Esc cancels). `^P` focuses the sidebar and `^E` the editor.

A `FILES` row under the editor and output panel lists every open buffer with
its `1`-`9` number — the active file is highlighted and unsaved files carry a
`*`. Press the number to switch to that file (plain digits in the sidebar,
`Alt+1`-`Alt+9` from anywhere, including while editing), then `^W` closes it.
Up to nine files can be open at once. Non-text files (`.pdf`, `.png`, `.docx`,
images, archives, executables, and any file whose content looks binary) are
never opened; they are marked `B` in the tree and produce a status message.

### Long names

The tree pane is narrow, so a long file or directory name does not fit. The
name is cut off with an ellipsis (`…`) and the arrow keys scroll it: Left and
Right move the text sideways instead of expanding and collapsing folders, and
the ellipsis on the other side marks the hidden part. The scroll only engages
for a selected row that is wider than the pane, so a tree of short names keeps
its usual keys. Enter still opens a file and toggles a folder either way.

A name is measured in terminal columns, so a CJK or emoji name takes the space
it really occupies and the tree stays aligned. Control characters in a name
are shown in caret notation (`^I` for a tab), so a file name can never move the
cursor or repaint the screen.

### Editor

The editor has line numbers and Lynxer syntax highlighting (`.lynx` files
only; every other file is plain text), Tab inserts four spaces, and Enter
splits the line.

**Long lines are soft-wrapped like a paragraph.** The text continues on the
next row and every row repeats the line number in the gutter, so a wrapped
line still reads as one line:

```text
   1 | fn main(){ println("a very long line that does not fit in the editor
   1 | column and continues here"); }
```

The caret follows the wrapped rows: Up and Down walk them one at a time and
keep the column inside the row, Home and End move to the start and the end of
the row, and the view scrolls in visual rows, so a wrapped line scrolls as one
block. The column in the status bar is the column on screen, not the offset
inside the whole line.

The caret also crosses line boundaries: Left at the start of a line jumps to
the end of the line above, Right at the end of a line jumps to the start of
the line below. Backspace at the start of a line joins it with the line above,
and Delete at the end of a line joins it with the line below. Backspace is
accepted as `BS` (8) and as `DEL` (127), the byte most terminals send, so the
key works on every layout.

### Output and problems

The output panel shows the output of the last command: `Run`, `Lint`,
`Format`, `Build` and `Run project` all shell out to this interpreter, and the
result is captured into the panel instead of overwriting the screen. `^L`
parses the `lynxer --lint` output into warnings and errors with their file,
line and column; `^V` jumps to the next problem, and clicking is not needed —
the message line names the file and position.

The panel stays hidden until a command produces output (`^Z` toggles it, `^K`
clears it).

**`^]` moves the focus to the output panel**, so the output of a run can be
read without leaving the file. The panel scrolls with the arrow keys,
`PgUp`/`PgDn` page through it and `Home`/`End` jump to its ends; any other
key hands the focus back to the file and is handled as usual. The panel that
has the focus is drawn on a lighter background, and the delimiter columns of
the tree and the editor carry a background of their own so the panes never
run into each other.

### Key bindings

`^` means Ctrl, `M-^C` means Ctrl+Alt+C.

| Key | Action |
|-----|--------|
| `^G` / `Esc` | help / close an overlay or prompt |
| `^O` / `^N` / `^S` / `^W` | open / new / save / close tab |
| `^X` / `^Q` | exit (`^Q` is an alias) |
| `^B` | change folder |
| `^P` / `^E` | focus sidebar / focus editor |
| `^Z` / `^K` | toggle output panel / clear output |
| `^]` | focus the output panel (and scroll it) |
| `^F` / `^_` / `^D` | find / go to line / next match |
| `^R` / `^L` / `^T` | run file / lint file / format file |
| `^Y` / `^U` | build project / run project |
| `^V` | next problem |
| `Ctrl+Alt+C` / `^A` / `^C` | command menu (all commands) |
| `1`-`9` / `Alt+1`-`Alt+9` | switch to the numbered open file (max 9) |
| `Ctrl+Left` / `Ctrl+Right` | previous / next tab |
| Explorer: Up/Down, Right, Left | select files/folders, step into folder, collapse / parent |
| Explorer: Left/Right (long name) | scroll the tree sideways |
| Explorer: Enter | open file / expand folder, or close an already-open one |
| Editor: arrows, Home/End, PgUp/PgDn, Tab, Backspace, Delete, Enter | move and edit |
| Command menu: Up/Down, PgUp/PgDn, Home/End, Enter, Esc | select / run / close |

## GUI IDE

`--gui-ide` launches the native IDE, a Rust program built with
[iced](https://iced.rs): no webview, no React, no Monaco. The launcher
resolves `lynxer-ide` next to the interpreter, in the installed
`<prefix>/lib/lynxer` layout, or in `lynxer/ide/target/{release,debug}`. Set
`LYNXER_IDE` to an explicit executable (or a directory containing it) to
override. The launcher passes its own absolute path as `--lynxer <path>`, so
Run / Lint / Format work even when `lynxer` is not on `PATH`. It takes no
arguments and returns the IDE's exit code.

```bash
lynxer --gui-ide
LYNXER_IDE=/path/to/lynxer-ide lynxer --gui-ide
```

The GUI IDE has a dark layout with a command bar, an activity rail, a
collapsible file explorer, styled tabs, a breadcrumb bar, a bottom panel with
**Problems** and **Output** tabs, and a status bar with the error and warning
counts, `Ln/Col`, encoding, language and interpreter version. It offers
autocomplete for keywords, types, the builtin registry and the words of the
current document, Rich Lynxer syntax highlighting, a configurable interpreter
path (**Set Lynxer**), and runs the interpreter off the UI thread with a 30
second timeout.

The IDE is a client of the interpreter: it needs `lynxer` on `PATH`, next to
the executable, or selected with **Set Lynxer**. Building it is a separate
step:

```bash
cd lynxer/ide
make build       # debug build
make release     # optimized build
```

See `lynxer/ide/README.md` for the full feature list and the build
dependencies.

## See also

- [CLI.md](CLI.md) — every flag of the `lynxer` executable
- [install.md](install.md) — build commands and artifacts
- [language-server.md](language-server.md) — the stdio language server, for
  editors that are not the bundled IDEs
