# tkinter

Comprehensive GUI toolkit wrapping Python's `tkinter` and `tkinter.ttk`. Build desktop applications with windows, widgets, menus, dialogs, and a drawing canvas.

> **Want a modern, dark-mode-ready UI?** Use the `ctk*` functions built into this module (e.g. `ctkInit`, `ctkLabel`, `ctkButton`). They wrap [CustomTkinter](https://github.com/TomSchimansky/CustomTkinter) and require `pip install customtkinter`. Do **not** mix standard `init`/`label`/… and `ctkInit`/`ctkLabel`/… in the same program — they each create their own root window.

Widgets are referenced by **integer indexes** stored in an internal list that is initialised by `init()`.

---

## Quick start

```c
global setup(){ import("tkinter"); }

global main(){
    global.tkinter.init("My App", 400, 300);
    int lbl = global.tkinter.label("Hello, World!");
    int btn = global.tkinter.button("Click me");
    global.tkinter.run();
}
```

---

## Window management

| Function | Signature | Description |
|----------|-----------|-------------|
| `init` | `init(str title, int width, int height)` | Create the root window. Must be called first. |
| `setTitle` | `setTitle(str title)` | Change the window title. |
| `resize` | `resize(int width, int height)` | Set window size. |
| `setBackground` | `setBackground(str color)` | Set window background colour. |
| `disableResize` | `disableResize()` | Prevent the user from resizing the window. |
| `setMinSize` | `setMinSize(int width, int height)` | Minimum allowed window size. |
| `setMaxSize` | `setMaxSize(int width, int height)` | Maximum allowed window size. |
| `setOpacity` | `setOpacity(float alpha)` | Opacity from `0.0` (invisible) to `1.0` (opaque). |
| `center` | `center()` | Centre the window on the screen. |
| `topmost` | `topmost(bool enabled)` | Keep window always on top. |
| `iconify` | `iconify()` | Minimise the window. |
| `deiconify` | `deiconify()` | Restore a minimised window. |
| `update` | `update()` | Force a UI refresh (process pending events). |
| `after` | `after(int delayMs)` | Block for `delayMs` milliseconds (simple delay). |
| `bindClose` | `bindClose(str event)` | Close the window when `event` fires. Use `"WM_DELETE_WINDOW"` for the × button, or a key like `"<Escape>"`. |

---

## Widgets (all return an integer index)

### Label

| Function | Signature | Description |
|----------|-----------|-------------|
| `label` | `label(str text)` | Create a text label. |
| `labelStyled` | `labelStyled(str text, str fg, str bg)` | Label with custom text and background colours. |

### Button

| Function | Signature | Description |
|----------|-----------|-------------|
| `button` | `button(str text)` | Create a button. |
| `buttonStyled` | `buttonStyled(str text, str fg, str bg)` | Button with custom colours. |

### Text input

| Function | Signature | Description |
|----------|-----------|-------------|
| `entry` | `entry()` | Single-line text entry (30 chars wide). |
| `passwordEntry` | `passwordEntry()` | Password entry — characters shown as `•`. |
| `textBox` | `textBox(int width, int height)` | Multi-line text box (characters × lines). |

### Checkbox & Radio

| Function | Signature | Description |
|----------|-----------|-------------|
| `checkbox` | `checkbox(str text)` | Checkbutton. Read state with `isChecked(idx)`. |
| `radioButton` | `radioButton(str text, str groupId)` | Radio button belonging to `groupId`. All buttons with the same `groupId` are mutually exclusive. Read selection with `getRadio(groupId)`. |

### Listbox

| Function | Signature | Description |
|----------|-----------|-------------|
| `listbox` | `listbox(str itemsJson)` | Listbox pre-populated from a JSON array of strings, e.g. `'["A","B","C"]'`. |

### Scale (slider)

| Function | Signature | Description |
|----------|-----------|-------------|
| `scale` | `scale(float lo, float hi)` | Horizontal slider. |
| `scaleVertical` | `scaleVertical(float lo, float hi)` | Vertical slider. |

### Spinbox

| Function | Signature | Description |
|----------|-----------|-------------|
| `spinbox` | `spinbox(float lo, float hi, float step)` | Numeric stepper widget. |

### ttk widgets

| Function | Signature | Description |
|----------|-----------|-------------|
| `progressBar` | `progressBar(float maxVal)` | Horizontal progress bar. |
| `combobox` | `combobox(str itemsJson)` | Drop-down selector from a JSON array of strings, e.g. `'["Red","Green","Blue"]'`. |
| `notebook` | `notebook()` | Tabbed-panel container. Add tabs with `addTab`. |

### Layout containers

| Function | Signature | Description |
|----------|-----------|-------------|
| `frame` | `frame()` | Plain invisible frame (container). |
| `labelFrame` | `labelFrame(str text)` | Frame with a labelled border. |
| `separator` | `separator()` | Horizontal rule line. |

### Canvas

| Function | Signature | Description |
|----------|-----------|-------------|
| `canvas` | `canvas(int width, int height)` | Drawing canvas (white background). |

### Image

| Function | Signature | Description |
|----------|-----------|-------------|
| `image` | `image(str imagePath)` | Display a PNG, GIF, or PPM image. Returns `-1` on failure. |

### Scrollbar

| Function | Signature | Description |
|----------|-----------|-------------|
| `scrollbar` | `scrollbar(int targetIdx)` | Create a vertical scrollbar and attach it to the widget at `targetIdx` (typically a `textBox` or `listbox`). |

---

## Tabs (Notebook)

```c
int nb  = global.tkinter.notebook();
int f1  = global.tkinter.frame();
int lbl = global.tkinter.label("Content of Tab 1");
int f2  = global.tkinter.frame();
global.tkinter.addTab(nb, f1, "Tab 1");
global.tkinter.addTab(nb, f2, "Tab 2");
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `addTab` | `addTab(int nbIdx, int frameIdx, str text)` | Add a frame as a tab in a Notebook. |

---

## Canvas drawing

| Function | Signature | Description |
|----------|-----------|-------------|
| `canvasLine` | `canvasLine(int idx, float x1, float y1, float x2, float y2, str color)` | Draw a line. |
| `canvasRect` | `canvasRect(int idx, float x1, float y1, float x2, float y2, str fill, str outline)` | Draw a filled rectangle. |
| `canvasOval` | `canvasOval(int idx, float x1, float y1, float x2, float y2, str fill, str outline)` | Draw a filled oval/circle. |
| `canvasText` | `canvasText(int idx, float x, float y, str text, str color)` | Draw text at `(x, y)`. |
| `canvasPolygon` | `canvasPolygon(int idx, str coordsJson, str fill, str outline)` | Draw a polygon from a JSON `[x,y,x,y,…]` coordinate array. |
| `canvasClear` | `canvasClear(int idx)` | Delete all shapes from the canvas. |

```c
int cv = global.tkinter.canvas(300, 200);
global.tkinter.canvasRect(cv, 10.0, 10.0, 100.0, 80.0, "blue", "black");
global.tkinter.canvasOval(cv, 110.0, 10.0, 200.0, 80.0, "red", "");
global.tkinter.canvasLine(cv, 0.0, 100.0, 300.0, 100.0, "green");
global.tkinter.canvasText(cv, 150.0, 150.0, "Hello Canvas", "black");
```

---

## Menus

```c
int mb   = global.tkinter.menubar();
int file = global.tkinter.addMenu("File");
global.tkinter.addMenuItem(file, "Open");
global.tkinter.addMenuItem(file, "Save");
global.tkinter.addMenuSeparator(file);
global.tkinter.addMenuItem(file, "Exit");

int edit = global.tkinter.addMenu("Edit");
global.tkinter.addCheckMenuItem(edit, "Word Wrap", true);
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `menubar` | `menubar()` | Create and attach the menu bar. Returns menu index. |
| `addMenu` | `addMenu(str text)` | Add a top-level menu (e.g. `"File"`). Returns menu index. |
| `addMenuItem` | `addMenuItem(int menuIdx, str text)` | Add a command item to a menu. |
| `addMenuSeparator` | `addMenuSeparator(int menuIdx)` | Add a separator line. |
| `addCheckMenuItem` | `addCheckMenuItem(int menuIdx, str text, bool isOn)` | Add a checkable item. |
| `contextMenu` | `contextMenu()` | Create a floating context menu. Returns menu index. |
| `addContextItem` | `addContextItem(int menuIdx, str text)` | Add an item to a context menu. |
| `showContext` | `showContext(int menuIdx, int x, int y)` | Show the context menu at screen position `(x, y)`. |

---

## Layout

By default widgets use `pack` (stacked vertically). Switch to grid or absolute placement:

| Function | Signature | Description |
|----------|-----------|-------------|
| `grid` | `grid(int idx, int row, int col)` | Place widget in a grid cell. |
| `gridSpan` | `gridSpan(int idx, int row, int col, int colspan)` | Grid cell with column span. |
| `place` | `place(int idx, int x, int y)` | Absolute pixel placement. |
| `pack` | `pack(int idx, str anchor)` | Re-pack with anchor `"w"`, `"e"`, `"center"`, etc. |

---

## Styling

| Function | Signature | Description |
|----------|-----------|-------------|
| `setFont` | `setFont(int idx, str family, int size, str style)` | Set font family, size, and style (`""`, `"bold"`, `"italic"`, `"bold italic"`). |
| `setForeground` | `setForeground(int idx, str color)` | Text colour. |
| `setBackgroundWidget` | `setBackgroundWidget(int idx, str color)` | Widget background colour. |
| `setPadding` | `setPadding(int idx, int px, int py)` | Internal horizontal and vertical padding in pixels. |
| `setCursor` | `setCursor(int idx, str cursor)` | Mouse cursor (`"arrow"`, `"hand2"`, `"crosshair"`, `"watch"`, …). |
| `setBorder` | `setBorder(int idx, str relief, int borderWidth)` | Border style: `"flat"`, `"raised"`, `"sunken"`, `"groove"`, `"ridge"`, `"solid"`. |

---

## Widget operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `getValue` | `getValue(int idx)` | String value from Entry, Text, Spinbox, or Combobox. |
| `getListSelection` | `getListSelection(int idx)` | Selected text from a Listbox. |
| `getListIndex` | `getListIndex(int idx)` | Zero-based index of the selected Listbox row, or `-1`. |
| `getScale` | `getScale(int idx)` | Float value of a Scale widget. |
| `setScale` | `setScale(int idx, float val)` | Set Scale value programmatically. |
| `getRadio` | `getRadio(str groupId)` | Text of the selected radio button in a group. |
| `getCombo` | `getCombo(int idx)` | Selected Combobox value. |
| `setCombo` | `setCombo(int idx, str text)` | Set Combobox selection by item text. |
| `getProgress` | `getProgress(int idx)` | Current ProgressBar value. |
| `setProgress` | `setProgress(int idx, float val)` | Set ProgressBar value. |
| `setText` | `setText(int idx, str text)` | Set Label/Button/Checkbutton display text. |
| `setEntryText` | `setEntryText(int idx, str text)` | Replace Entry content. |
| `setTextBoxContent` | `setTextBoxContent(int idx, str text)` | Replace entire Text widget content. |
| `isChecked` | `isChecked(int idx)` | `true` if Checkbutton is checked. |
| `setChecked` | `setChecked(int idx, bool checked)` | Programmatically check/uncheck a Checkbutton. |
| `clearWidget` | `clearWidget(int idx)` | Clear Entry, Text, or Listbox content. |
| `disableWidget` | `disableWidget(int idx)` | Grey out and disable a widget. |
| `enableWidget` | `enableWidget(int idx)` | Re-enable a disabled widget. |
| `hideWidget` | `hideWidget(int idx)` | Remove widget from layout (not destroyed). |
| `showWidget` | `showWidget(int idx)` | Re-show a hidden widget. |
| `destroyWidget` | `destroyWidget(int idx)` | Permanently destroy and remove a widget. |
| `focusWidget` | `focusWidget(int idx)` | Give keyboard focus to a widget. |

---

## Dialogs

| Function | Signature | Description |
|----------|-----------|-------------|
| `messageBox` | `messageBox(str title, str message)` | Info message box. |
| `warningBox` | `warningBox(str title, str message)` | Warning message box. |
| `errorBox` | `errorBox(str title, str message)` | Error message box. |
| `askYesNo` | `askYesNo(str title, str question)` | Yes/No dialog. Returns `bool`. |
| `askOkCancel` | `askOkCancel(str title, str message)` | OK/Cancel dialog. Returns `bool`. |
| `openFileDialog` | `openFileDialog(str title)` | File-open dialog. Returns path or `""`. |
| `openFileDialogFilter` | `openFileDialogFilter(str title, str ext)` | File-open filtered by extension, e.g. `".txt"`. |
| `saveFileDialog` | `saveFileDialog(str title)` | File-save dialog. Returns path or `""`. |
| `openDirDialog` | `openDirDialog(str title)` | Directory-chooser dialog. Returns path or `""`. |
| `askColor` | `askColor(str title)` | Colour picker. Returns hex string e.g. `"#ff0000"`, or `""`. |
| `askString` | `askString(str title, str prompt)` | Text-input dialog. Returns string or `""`. |
| `askInteger` | `askInteger(str title, str prompt)` | Integer-input dialog. Returns `int` or `0`. |
| `askFloat` | `askFloat(str title, str prompt)` | Float-input dialog. Returns `float` or `0.0`. |

---

## App lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `run` | `run()` | Start the Tk main event loop (blocking). |
| `close` | `close()` | Destroy the window and exit the loop. |

---

## Full example

```c
global setup(){ import("tkinter"); }

global main(){
    global.tkinter.init("Counter", 300, 220);
    global.tkinter.center();
    global.tkinter.setBackground("#f0f0f0");
    global.tkinter.disableResize();

    int title = global.tkinter.label("Score:");
    global.tkinter.setFont(title, "Helvetica", 16, "bold");

    int display = global.tkinter.label("0");
    global.tkinter.setFont(display, "Helvetica", 32, "");

    int slider = global.tkinter.scale(0.0, 100.0);
    int combo  = global.tkinter.combobox("[\"Easy\",\"Medium\",\"Hard\"]");
    int chk    = global.tkinter.checkbox("Enable sound");

    int btn = global.tkinter.button("Get values");

    global.tkinter.run();
}
```

---

## CustomTkinter (CTk) extension

All CTk functions are built into the same `tkinter` module — no separate import needed. Just `import("tkinter")` and use the `ctk*` functions. They maintain their own widget list (`builtins._lx_ctk_widgets`) with indexes independent of the standard widget list.

> **Requires:** `pip install customtkinter`  
> Do **not** call `init` and `ctkInit` in the same program — each creates its own root window.

### Quick start (CTk)

```c
global setup(){ import("tkinter"); }

global main(){
    global.tkinter.ctkSetAppearance("dark");
    global.tkinter.ctkSetTheme("blue");
    global.tkinter.ctkInit("My Modern App", 480, 360);
    global.tkinter.ctkCenter();

    int lbl  = global.tkinter.ctkLabelStyled("Hello, CTk!", "#5bc0eb", 20);
    int entry = global.tkinter.ctkEntryPlaceholder("Type something…");
    int sw   = global.tkinter.ctkSwitch("Dark mode");
    global.tkinter.ctkSetSwitch(sw, true);
    int prog = global.tkinter.ctkProgressBar();
    global.tkinter.ctkSetProgress(prog, 0.7);
    int btn  = global.tkinter.ctkButton("Save");

    global.tkinter.ctkRun();
}
```

### Theme

| Function | Signature | Description |
|----------|-----------|-------------|
| `ctkSetAppearance` | `ctkSetAppearance(str mode)` | `"dark"`, `"light"`, or `"system"`. Call before `ctkInit`. |
| `ctkSetTheme` | `ctkSetTheme(str theme)` | `"blue"`, `"green"`, or `"dark-blue"`. Call before `ctkInit`. |

### Window management

| Function | Signature | Description |
|----------|-----------|-------------|
| `ctkInit` | `ctkInit(str title, int width, int height)` | Create the CTk root window. |
| `ctkSetTitle` | `ctkSetTitle(str title)` | Change the window title. |
| `ctkResize` | `ctkResize(int width, int height)` | Set window size. |
| `ctkSetBackground` | `ctkSetBackground(str color)` | Set window background colour. |
| `ctkDisableResize` | `ctkDisableResize()` | Prevent user resizing. |
| `ctkSetMinSize` | `ctkSetMinSize(int width, int height)` | Minimum window size. |
| `ctkSetMaxSize` | `ctkSetMaxSize(int width, int height)` | Maximum window size. |
| `ctkSetOpacity` | `ctkSetOpacity(float alpha)` | Opacity 0.0–1.0. |
| `ctkCenter` | `ctkCenter()` | Centre on screen. |
| `ctkTopmost` | `ctkTopmost(bool enabled)` | Keep always on top. |
| `ctkIconify` | `ctkIconify()` | Minimise. |
| `ctkDeiconify` | `ctkDeiconify()` | Restore. |
| `ctkUpdate` | `ctkUpdate()` | Force UI refresh. |
| `ctkAfter` | `ctkAfter(int delayMs)` | Block for ms. |
| `ctkBindClose` | `ctkBindClose(str event)` | Close on event (`"WM_DELETE_WINDOW"`, `"<Escape>"`, …). |

### CTk widgets (all return an integer index)

| Function | Signature | Description |
|----------|-----------|-------------|
| `ctkLabel` | `ctkLabel(str text)` | CTkLabel. |
| `ctkLabelStyled` | `ctkLabelStyled(str text, str color, int size)` | Label with colour and font size. |
| `ctkButton` | `ctkButton(str text)` | CTkButton. |
| `ctkButtonStyled` | `ctkButtonStyled(str text, str fg, str hover)` | Button with fill and hover colour. |
| `ctkEntry` | `ctkEntry()` | Single-line text input. |
| `ctkEntryPlaceholder` | `ctkEntryPlaceholder(str placeholder)` | Entry with placeholder text. |
| `ctkPasswordEntry` | `ctkPasswordEntry()` | Password entry (`•`). |
| `ctkTextBox` | `ctkTextBox(int width, int height)` | Multi-line textbox. |
| `ctkCheckbox` | `ctkCheckbox(str text)` | CTkCheckBox. Read with `ctkIsChecked(idx)`. |
| `ctkSwitch` | `ctkSwitch(str text)` | CTkSwitch. Read with `ctkIsOn(idx)`. |
| `ctkRadioButton` | `ctkRadioButton(str text, str groupId)` | CTkRadioButton. Read with `ctkGetRadio(groupId)`. |
| `ctkCombobox` | `ctkCombobox(str itemsJson)` | Dropdown — JSON array. Read with `ctkGetCombo(idx)`. |
| `ctkOptionMenu` | `ctkOptionMenu(str itemsJson)` | Option menu. Read with `ctkGetOption(idx)`. |
| `ctkSegmented` | `ctkSegmented(str valuesJson)` | Segmented button bar. Read with `ctkGetValue(idx)`. |
| `ctkSlider` | `ctkSlider(float lo, float hi)` | CTkSlider. Read with `ctkGetSlider(idx)`. |
| `ctkProgressBar` | `ctkProgressBar()` | CTkProgressBar (0.0–1.0). Set with `ctkSetProgress(idx, val)`. |
| `ctkFrame` | `ctkFrame()` | CTkFrame container. |
| `ctkScrollFrame` | `ctkScrollFrame(int width, int height)` | CTkScrollableFrame. |
| `ctkTabView` | `ctkTabView()` | CTkTabview. Add tabs with `ctkAddTab(tvIdx, text)`. |
| `ctkAddTab` | `ctkAddTab(int tvIdx, str text)` | Add a tab to a CTkTabview. |
| `ctkImage` | `ctkImage(str imagePath, int width, int height)` | Display image (requires Pillow). |

### CTk layout

| Function | Signature | Description |
|----------|-----------|-------------|
| `ctkGrid` | `ctkGrid(int idx, int row, int col)` | Grid position. |
| `ctkGridSpan` | `ctkGridSpan(int idx, int row, int col, int colspan)` | Grid with column span. |
| `ctkPlace` | `ctkPlace(int idx, int x, int y)` | Absolute pixel position. |
| `ctkPack` | `ctkPack(int idx, str anchor)` | Re-pack with anchor. |

### CTk styling

| Function | Signature | Description |
|----------|-----------|-------------|
| `ctkSetFont` | `ctkSetFont(int idx, str family, int size, str style)` | Font (`style`: `"bold"`, `"italic"`, `"bold italic"`, `""`). |
| `ctkSetForeground` | `ctkSetForeground(int idx, str color)` | Text colour. |
| `ctkSetBackgroundWidget` | `ctkSetBackgroundWidget(int idx, str color)` | Widget `fg_color`. |
| `ctkSetCornerRadius` | `ctkSetCornerRadius(int idx, int radius)` | Corner radius in pixels. |
| `ctkSetBorderWidth` | `ctkSetBorderWidth(int idx, int width)` | Border width in pixels. |
| `ctkSetPadding` | `ctkSetPadding(int idx, int px, int py)` | Internal padding. |

### CTk widget operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `ctkGetValue` | `ctkGetValue(int idx)` | Entry / Textbox / ComboBox / SegmentedButton value. |
| `ctkGetText` | `ctkGetText(int idx)` | Label or Button display text. |
| `ctkSetText` | `ctkSetText(int idx, str text)` | Set display text. |
| `ctkSetEntryText` | `ctkSetEntryText(int idx, str text)` | Replace Entry content. |
| `ctkSetTextBoxContent` | `ctkSetTextBoxContent(int idx, str text)` | Replace Textbox content. |
| `ctkIsChecked` | `ctkIsChecked(int idx)` | `true` if CheckBox checked. |
| `ctkSetChecked` | `ctkSetChecked(int idx, bool checked)` | Check/uncheck. |
| `ctkIsOn` | `ctkIsOn(int idx)` | `true` if Switch is on. |
| `ctkSetSwitch` | `ctkSetSwitch(int idx, bool on)` | Turn switch on/off. |
| `ctkGetRadio` | `ctkGetRadio(str groupId)` | Selected radio value. |
| `ctkGetSlider` | `ctkGetSlider(int idx)` | Slider value. |
| `ctkSetSlider` | `ctkSetSlider(int idx, float val)` | Set slider value. |
| `ctkGetCombo` | `ctkGetCombo(int idx)` | ComboBox selection. |
| `ctkSetCombo` | `ctkSetCombo(int idx, str text)` | Set ComboBox selection. |
| `ctkGetOption` | `ctkGetOption(int idx)` | OptionMenu selection. |
| `ctkSetOption` | `ctkSetOption(int idx, str text)` | Set OptionMenu selection. |
| `ctkSetProgress` | `ctkSetProgress(int idx, float val)` | Set progress (0.0–1.0). |
| `ctkGetProgress` | `ctkGetProgress(int idx)` | Get progress value. |
| `ctkClearWidget` | `ctkClearWidget(int idx)` | Clear Entry or Textbox. |
| `ctkDisableWidget` | `ctkDisableWidget(int idx)` | Disable (grey out). |
| `ctkEnableWidget` | `ctkEnableWidget(int idx)` | Re-enable. |
| `ctkHideWidget` | `ctkHideWidget(int idx)` | Remove from layout. |
| `ctkShowWidget` | `ctkShowWidget(int idx)` | Re-pack hidden widget. |
| `ctkDestroyWidget` | `ctkDestroyWidget(int idx)` | Permanently destroy. |
| `ctkFocusWidget` | `ctkFocusWidget(int idx)` | Keyboard focus. |

### CTk app lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `ctkRun` | `ctkRun()` | Start the CTk event loop (blocking). |
| `ctkClose` | `ctkClose()` | Destroy window and exit the loop. |
