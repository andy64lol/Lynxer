# turtle

Comprehensive wrapper for Python's `turtle` graphics module. Draw vector graphics, shapes, spirals, text, and interactive scenes on a resizable canvas.

Requires a display (Tk). Always call `done()` at the end of `main()` to keep the window open.

---

## Quick start

```c
global setup(){ import("turtle"); }

global main(){
    global.turtle.init(600, 400);
    global.turtle.title("My Drawing");
    global.turtle.bgcolor("black");
    global.turtle.pencolor("cyan");
    global.turtle.pensize(2.0);
    global.turtle.speed(6);

    for(int i = 0; i < 4; i = i + 1){
        global.turtle.forward(100.0);
        global.turtle.right(90.0);
    }

    global.turtle.done();
}
```

---

## Window & screen

| Function | Signature | Description |
|----------|-----------|-------------|
| `init` | `init(int width, int height)` | Set canvas window size in pixels. |
| `title` | `title(str text)` | Set the window title. |
| `bgcolor` | `bgcolor(str color)` | Set background colour, e.g. `"white"` or `"#1a1a2e"`. |
| `bgpic` | `bgpic(str filename)` | Set a background image (GIF or PNG). |
| `screensize` | `screensize(int width, int height)` | Set the scrollable canvas size. |
| `window_width` | `window_width()` | Return window width in pixels. |
| `window_height` | `window_height()` | Return window height in pixels. |
| `mode` | `mode(str m)` | Set drawing mode: `"standard"` (0° = East, CCW+), `"logo"` (0° = North, CW+), `"world"` (user coords). |
| `colormode` | `colormode(int n)` | Set colour resolution: `255` for 0–255 RGB, `1` for 0.0–1.0. |
| `tracer` | `tracer(int n, int delay)` | Animation control. `n=0` disables animation (fastest); `n=1` redraws every step. `delay` is ms between frames. |
| `update` | `update()` | Force a screen redraw. Use after `tracer(0, 0)`. |

---

## Movement

| Function | Signature | Description |
|----------|-----------|-------------|
| `forward` | `forward(float dist)` | Move forward `dist` pixels. |
| `backward` | `backward(float dist)` | Move backward `dist` pixels. |
| `right` | `right(float angle)` | Turn right `angle` degrees. |
| `left` | `left(float angle)` | Turn left `angle` degrees. |
| `goto` | `goto(float x, float y)` | Move to absolute position; pen state preserved. |
| `setx` | `setx(float x)` | Set x coordinate without changing y. |
| `sety` | `sety(float y)` | Set y coordinate without changing x. |
| `home` | `home()` | Move to (0, 0) and reset heading to 0. |
| `setheading` | `setheading(float angle)` | Set absolute heading in degrees. |
| `towards` | `towards(float x, float y)` | Return the angle from the turtle towards `(x, y)`. |

---

## Pen control

| Function | Signature | Description |
|----------|-----------|-------------|
| `penup` | `penup()` | Lift pen — movement does not draw. |
| `pendown` | `pendown()` | Lower pen — movement draws. |
| `pencolor` | `pencolor(str color)` | Set pen colour. |
| `pensize` | `pensize(float size)` | Set pen width in pixels. |
| `color` | `color(str penColor, str fillColor)` | Set pen and fill colour simultaneously. |
| `fillcolor` | `fillcolor(str color)` | Set fill colour. |
| `begin_fill` | `begin_fill()` | Start recording a filled shape. |
| `end_fill` | `end_fill()` | Close and fill the shape. |
| `isdown` | `isdown()` | `true` if the pen is currently down. |
| `speed` | `speed(int n)` | Drawing speed: `0` = fastest (no animation), `1` = slowest, `6` = normal, `10` = fast. |

---

## Drawing primitives

| Function | Signature | Description |
|----------|-----------|-------------|
| `circle` | `circle(float r)` | Draw a circle of radius `r`. Positive = CCW, negative = CW. |
| `arc` | `arc(float r, float extent)` | Draw an arc of radius `r` spanning `extent` degrees. |
| `dot` | `dot(float size)` | Draw a dot of diameter `size` in the current pen colour. |
| `dotColor` | `dotColor(float size, str color)` | Draw a dot in an explicit colour. |
| `write` | `write(str text)` | Write text at the current position. |
| `writeFont` | `writeFont(str text, str fontName, int fontSize, str fontStyle)` | Write text with an explicit font. `fontStyle`: `"normal"`, `"bold"`, `"italic"`. |
| `writeAligned` | `writeAligned(str text, str align, str fontName, int fontSize)` | Write text with alignment: `"left"`, `"center"`, or `"right"`. |

---

## Stamps

| Function | Signature | Description |
|----------|-----------|-------------|
| `stamp` | `stamp()` | Stamp the turtle shape onto the canvas. Returns stamp ID. |
| `clearstamp` | `clearstamp(int stampId)` | Remove the stamp with the given ID. |
| `undo` | `undo()` | Undo the last turtle action. |

---

## Turtle appearance

| Function | Signature | Description |
|----------|-----------|-------------|
| `shape` | `shape(str name)` | Set the turtle shape: `"arrow"`, `"turtle"`, `"circle"`, `"square"`, `"triangle"`, `"classic"`. |
| `addshape` | `addshape(str name, str filename)` | Register a custom turtle shape from a GIF file. Use `shape(name)` to activate it. |
| `turtlesize` | `turtlesize(float stretchWid, float stretchLen, float outline)` | Resize the turtle glyph. `stretchWid` is perpendicular to heading, `stretchLen` along heading. |
| `resizemode` | `resizemode(str mode)` | `"noresize"` (default), `"auto"` (scales with pen width), `"user"` (use `turtlesize`). |
| `hideturtle` | `hideturtle()` | Hide the turtle arrow. |
| `showturtle` | `showturtle()` | Show the turtle arrow. |

---

## Turtle state

| Function | Signature | Description |
|----------|-----------|-------------|
| `xcor` | `xcor()` | Current x coordinate. |
| `ycor` | `ycor()` | Current y coordinate. |
| `heading` | `heading()` | Current heading in degrees. |
| `pos` | `pos()` | Current position as `"(x, y)"` string. |
| `distance` | `distance(float x, float y)` | Distance from turtle to `(x, y)`. |

---

## Screen clearing

| Function | Signature | Description |
|----------|-----------|-------------|
| `clear` | `clear()` | Erase all drawings; keep turtle position and heading. |
| `reset` | `reset()` | Erase drawings and reset turtle to origin. |

---

## Event handling

| Function | Signature | Description |
|----------|-----------|-------------|
| `listen` | `listen()` | Give the screen keyboard focus. Call before `onkey`. |
| `onkey` | `onkey(str key)` | Bind a key press to a log action. Key examples: `"Up"`, `"space"`, `"Return"`, `"q"`. For real callbacks use `rawPy` + `_t.onkey(fn, key)`. |
| `onclick` | `onclick()` | Bind a turtle click to a log action. |
| `onscreenclick` | `onscreenclick()` | Bind a screen click to a log action. |
| `exitonclick` | `exitonclick()` | Exit the window when the user clicks anywhere. |

---

## Input dialogs

| Function | Signature | Description |
|----------|-----------|-------------|
| `numinput` | `numinput(str title, str prompt)` | Numeric input dialog. Returns `float`, or `-1.0` if cancelled. |
| `textinput` | `textinput(str title, str prompt)` | Text input dialog. Returns `str`, or `""` if cancelled. |

---

## High-level shape helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `polygon` | `polygon(int nSides, float sideLen)` | Draw a filled regular polygon. |
| `star` | `star(int nPoints, float r)` | Draw a filled regular star inscribed in a circle of radius `r`. |
| `grid` | `grid(int rows, int cols, float cellSize)` | Draw a rows × cols grid centred at the current position. |
| `spiral` | `spiral(int nLines, float startLen, float deltaLen, float angle)` | Draw a spiral of `nLines` lines, each `deltaLen` pixels longer than the last. |

---

## App lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `done` | `done()` | Enter the Tk event loop — keeps the window open. Always call at the end of `main()`. |

---

## Examples

### Coloured spiral

```c
global setup(){ import("turtle"); }

global main(){
    global.turtle.init(600, 600);
    global.turtle.bgcolor("black");
    global.turtle.speed(0);
    global.turtle.tracer(0, 0);

    any colors = ["red", "orange", "yellow", "green", "cyan", "blue", "violet"];
    for(int i = 0; i < 200; i = i + 1){
        global.turtle.pencolor(listGet(colors, i % 7));
        global.turtle.forward(i * 1.5);
        global.turtle.right(91.0);
    }

    global.turtle.update();
    global.turtle.done();
}
```

### Interactive input

```c
global setup(){ import("turtle"); }

global main(){
    global.turtle.init(500, 400);
    global.turtle.title("Draw");

    float sides = global.turtle.numinput("Polygon", "How many sides?");
    float size  = global.turtle.numinput("Polygon", "Side length?");

    global.turtle.speed(6);
    global.turtle.polygon(intOf(strOf(sides)), size);
    global.turtle.done();
}
```

### Grid background

```c
global setup(){ import("turtle"); }

global main(){
    global.turtle.init(600, 600);
    global.turtle.speed(0);
    global.turtle.tracer(0, 0);
    global.turtle.pencolor("#cccccc");
    global.turtle.pensize(1.0);
    global.turtle.grid(10, 10, 40.0);
    global.turtle.update();
    global.turtle.done();
}
```
