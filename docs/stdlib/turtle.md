# turtle

Classic turtle graphics: a pen that moves and turns, drawing as it goes.

**Backend:** pure — `stdlib/turtle.lynx`, a thin wrapper over the `graphics`
module's turtle state machine (`rust/graphics`). The drawing goes through the
ordinary shape ops, so it works with a window *and* on the headless rasterizer.
**Import:** `import("turtle")` → `global.turtle.*`

## State lives in `graphics`

Lynxer has no module-level mutable state, so a pure-Lynxer module cannot
remember anything between calls. The turtle therefore lives in the `graphics`
module as a handle; this module drives the **implicit default turtle**
(handle `0`), which `graphics` creates on first use. Programs that need several
turtles use the `graphics.turtle*` ops with an explicit handle from
`graphics.turtleCreate(x, y, heading)`.

The heading is in degrees: `0` points east (`+x`) and increases clockwise,
matching the screen's y-down coordinate system.

## Functions

| Function | Signature | Notes |
| --- | --- | --- |
| `forward` | `(float distance) -> int` | Move forward, drawing when the pen is down |
| `back` | `(float distance) -> int` | Move backward |
| `turn` / `turnRight` | `(float degrees) -> int` | Turn clockwise |
| `turnLeft` | `(float degrees) -> int` | Turn counter-clockwise |
| `goto` | `(float x, float y) -> int` | Move straight without turning |
| `home` | `() -> int` | Return to the origin, facing east |
| `penUp` / `penDown` | `() -> int` | Stop / resume drawing while moving |
| `setColor` | `(int r, int g, int b, int a = 255) -> int` | Pen colour |
| `setWidth` | `(float width) -> int` | Pen width in pixels |
| `x` / `y` | `() -> float` | Current position |
| `heading` | `() -> float` | Current heading in degrees |
| `penIsDown` | `() -> bool` | Whether the pen is down |

Each op returns `0` on success (the movement ops) or the queried value; there is
no failure sentinel because the default turtle always exists.

## Example

```lynx
global setup(){ import("turtle"); import("graphics"); }

global main(){
    global.graphics.init("turtle", 400, 400);
    global.graphics.clearBackground(255, 255, 255);

    global.turtle.setColor(0, 90, 200);
    global.turtle.setWidth(2.0);
    for (int i = 0; i < 4; i = i + 1){
        global.turtle.forward(120.0);
        global.turtle.turn(90.0);
    }

    println(global.turtle.x(), ",", global.turtle.y());
    println(global.turtle.heading());
}
```

---

## See also

- [graphics.md](graphics.md) — the drawing surface the turtle moves over, and
  the explicit-handle `graphics.turtle*` ops.
- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [limitations.md](../limitations.md) — the full divergence register.
