# tkinter

Simple wrappers around Python's `tkinter` for quick GUI elements.

Usage pattern:
1. `init(title, width, height)` — create root window.
2. Call widget-creation helpers to obtain widget indexes.
3. Use `getValue`, `setText`, `isChecked`, etc., to interact with widgets.
4. `run()` starts the main loop; `close()` destroys the window.

Widget helpers (return widget index):
- `label(text)`, `button(text)`, `entry()`, `textBox(width,height)`, `checkbox(text)`, `separator()`, `frame()`.

Widget operations:
- `getValue(idx)`, `setText(idx, text)`, `clearWidget(idx)`, `disableWidget(idx)`, `enableWidget(idx)`, `isChecked(idx)`.

Dialogs and utilities:
- `messageBox(title,msg)`, `warningBox(title,msg)`, `errorBox(title,msg)`, `askYesNo(title,question)`.
- `openFileDialog(title)`, `saveFileDialog(title)`, `setTitle(title)`, `resize(w,h)`, `setBackground(color)`, `disableResize()`.

Notes:
- Widgets are referenced by integer indexes stored in an internal list; do not rely on stable indexes across runs.