# turtle

Wrappers for Python's `turtle` graphics module.

Quick usage:
- `init(width, height)` — prepare canvas.
- Use drawing commands like `forward`, `right`, `left`, `goto`, `pendown`, `penup`.
- Call `done()` at the end of your program to enter the event loop and keep the window open.

Selected functions:
- Initialization and window: `init(w,h)`, `title(text)`, `bgcolor(color)`, `screensize(w,h)`, `window_width()`, `window_height()`.
- Movement: `forward(dist)`, `backward(dist)`, `right(angle)`, `left(angle)`, `goto(x,y)`, `home()`.
- Pen and drawing: `penup()`, `pendown()`, `pencolor(color)`, `pensize(size)`, `fillcolor(color)`, `begin_fill()`, `end_fill()`, `circle(r)`, `dot(size)`, `write(text)`.
- State and utility: `speed(n)`, `hideturtle()`, `showturtle()`, `clear()`, `reset()`, `done()`, `xcor()`, `ycor()`, `heading()`, `pos()`, `distance(x,y)`, `stamp()`, `clearstamp(id)`, `undo()`, `tracer(n,delay)`, `update()`, `isdown()`, `mode(m)`, `colormode(n)`, `shape(name)`, `color(pen,fill)`.

Notes:
- Requires a display (Tk). Use `done()` to keep the window visible.