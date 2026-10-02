# game

2-D game toolkit for Lynxer. The drawing, input and window layer is a Rust
library built on [macroquad](https://macroquad.rs), linked into
`stdlib/game.so` and reached through `global.game`.

> **Requires:** a Rust toolchain (`cargo`), which is required for the whole
> build. `stdlib/game.so` is built by `cargo` like every other Rust backend; a
> missing toolchain fails `make` instead of skipping the module. The Rust source
> lives in `rust/game/`; build it with `make cargo` or `make buildLynxer`.

---

## Coordinates

World coordinates use **bottom-left origin, +Y up**, matching the reference
`game` module. The Rust layer flips Y for macroquad internally, and mouse
coordinates are reported from the bottom-left. Angles are degrees,
counter-clockwise.

## Frames and callbacks

`run()` is blocking. Register the two frame callbacks by name, then call
`run()`:

```lynx
global setup(){ import("game"); }

global onUpdate(float dt){
    // advance the world; dt is seconds since the previous frame
}

global onDraw(){
    global.game.beginDraw();
    global.game.drawRect(100.0, 100.0, 40.0, 40.0, 255, 0, 0);
    global.game.drawText("hello", 10.0, 10.0, 255, 255, 255, 16);
    global.game.endDraw();
}

global main(){
    global.game.init("Demo", 800, 600);
    global.game.setBackground(20, 20, 30);
    global.game.setUpdateCallback("onUpdate");
    global.game.setDrawCallback("onDraw");
    global.game.run();
}
```

Each frame the module calls `onUpdate(dt)` and then `onDraw()`. If either
callback raises an error the loop stops and the error is reported at the
`run()` call site. Ctrl-C stops the loop and exits with code 130.

## Headless mode

Set `LYNXER_GAME_HEADLESS=1` to run without a window. `run()` then executes a
fixed number of deterministic frames (`dt = 1/60`) and every drawing call is a
no-op, so the API and the callback bridge can be tested without a display. The
`make test` fixture runner sets this variable; see
`examples/stdlib_game.lynx`.

---

## Window

Alongside `setTitle`, `setBackground`, `getWidth`/`getHeight`, `setWindowSize`,
`setResizable`, `setMouseVisible`, `setFPSCap`, `getFPS`, `setFullscreen`,
`hideCursor`/`showCursor` and `close`:

| Function | Description |
|----------|-------------|
| `setWindowPos(x, y)` | Move the window (a no-op headless) |
| `screenshot(path)` | Save the current frame as a PNG; `-1` headless |

## Lifecycle and timing

| Function | Signature | Description |
|----------|-----------|-------------|
| `init` | `init(str title, int width, int height)` | Configure the window. Call first. |
| `setTitle` | `setTitle(str title)` | Set the title used when `run()` opens the window. |
| `setBackground` | `setBackground(int r, int g, int b)` | Clear colour (0–255). |
| `getWidth` / `getHeight` | `getWidth() -> int` | Current window size. |
| `setWindowSize` | `setWindowSize(int width, int height)` | Resize the window. |
| `setResizable` | `setResizable(bool enabled)` | Accepted and ignored (window is resizable by default). |
| `setFullscreen` | `setFullscreen(bool enabled)` | Enter/leave fullscreen. |
| `setMouseVisible` | `setMouseVisible(bool visible)` | Show/hide the cursor. |
| `hideCursor` / `showCursor` | `hideCursor()` | Cursor convenience wrappers. |
| `setFPSCap` | `setFPSCap(int fps)` | Frame-rate cap (0 disables). |
| `getFPS` | `getFPS() -> int` | Current FPS (0 headless). |
| `close` | `close()` | Ask the loop to stop after the current frame. |
| `isOpen` | `isOpen() -> bool` | True while the window is open. |


| Function | Signature | Description |
|----------|-----------|-------------|
| `run` | `run()` | Start the blocking frame loop. |
| `setUpdateCallback` | `setUpdateCallback(str name)` | Per-frame callback receiving `dt`. |
| `setDrawCallback` | `setDrawCallback(str name)` | Per-frame draw callback. |
| `deltaTime` | `deltaTime() -> float` | Seconds since the previous frame. |
| `getTime` | `getTime() -> float` | Seconds since the program started. |
| `beginDraw` / `endDraw` | `beginDraw()` | Clear the screen / no-op. |

## Shapes

All colours are `int r, g, b` (0–255); all coordinates are world coordinates.

| Function | Signature |
|----------|-----------|
| `drawRect` | `drawRect(float cx, float cy, float w, float h, int r, int g, int b)` |
| `drawRectOutline` | `drawRectOutline(cx, cy, w, h, r, g, b, float lineWidth)` |
| `drawCircle` | `drawCircle(cx, cy, float radius, r, g, b)` |
| `drawCircleOutline` | `drawCircleOutline(cx, cy, radius, r, g, b, lineWidth)` |
| `drawEllipse` | `drawEllipse(cx, cy, float w, float h, r, g, b)` |
| `drawEllipseOutline` | `drawEllipseOutline(cx, cy, w, h, r, g, b, lineWidth)` |
| `drawLine` | `drawLine(float x1, float y1, float x2, float y2, r, g, b, lineWidth)` |
| `drawTriangle` | `drawTriangle(x1, y1, x2, y2, x3, y3, r, g, b)` |
| `drawTriangleOutline` | `drawTriangleOutline(x1, y1, x2, y2, x3, y3, r, g, b, lineWidth)` |
| `drawPoint` | `drawPoint(float x, float y, r, g, b, float size)` |
| `drawRectRoundedFilled` | `drawRectRoundedFilled(cx, cy, w, h, r, g, b, float cornerRadius)` |
| `drawRectRoundedOutline` | `drawRectRoundedOutline(cx, cy, w, h, r, g, b, cornerRadius, lineWidth)` |
| `drawStar` | `drawStar(cx, cy, float outerR, float innerR, int points, r, g, b)` |
| `drawDashedLine` | `drawDashedLine(x1, y1, x2, y2, r, g, b, lineWidth, float dashLength)` |
| `drawCross` | `drawCross(cx, cy, float size, r, g, b, lineWidth)` |
| `drawGradientRect` | `drawGradientRect(cx, cy, w, h, r1, g1, b1, r2, g2, b2)` |
| `drawArc` | `drawArc(cx, cy, w, h, r, g, b, float startAngle, float endAngle, lineWidth)` |
| `drawArcFilled` | `drawArcFilled(cx, cy, w, h, r, g, b, startAngle, endAngle)` |
| `drawPolygon` | `drawPolygon(str coords, r, g, b)` |
| `drawPolygonOutline` | `drawPolygonOutline(str coords, r, g, b, lineWidth)` |
| `drawPolyline` | `drawPolyline(str coords, r, g, b, lineWidth)` |
| `drawPoints` | `drawPoints(str coords, r, g, b, float size)` |
| `drawLines` | `drawLines(str segments, r, g, b, lineWidth)` |

`coords` is a flat `"x,y,x,y,..."` list and `segments` a flat
`"x1,y1,x2,y2,..."` list of endpoints. (The reference module took JSON; this
backend accepts any non-numeric separator, so `"1,2 3,4"` and `"[1,2,3,4]"`
both work.)

## Text

| Function | Signature | Description |
|----------|-----------|-------------|
| `drawText` | `drawText(str text, float x, float y, r, g, b, int size)` | Bottom-left anchored at `(x, y)`. |
| `drawTextStyled` | `drawTextStyled(text, x, y, r, g, b, size, str font, bool bold, bool italic, str anchorX)` | `anchorX` is `"left"`/`"center"`/`"right"`; `font` is ignored. |
| `drawTextAnchored` | `drawTextAnchored(text, x, y, r, g, b, size, str anchorX, str anchorY)` | `anchorY` is `"bottom"`/`"center"`/`"top"`. |

## Input

Key names are case-insensitive: `"UP"`, `"DOWN"`, `"LEFT"`, `"RIGHT"`,
`"SPACE"`, `"ENTER"`, `"ESCAPE"`, `"TAB"`, `"BACKSPACE"`, `"SHIFT"`, `"CTRL"`,
`"ALT"`, `"A"`–`"Z"`, `"0"`–`"9"`, `"F1"`–`"F12"`. Button names are `"LEFT"`,
`"RIGHT"`, `"MIDDLE"`.

| Function | Signature |
|----------|-----------|
| `keyDown` / `keyUp` | `(str key) -> bool` |
| `keyPressed` / `keyReleased` | `(str key) -> bool` (once per event) |
| `keyCode` | `(str key) -> int` (backend key code, `-1` unknown) |
| `mouseX` / `mouseY` | `() -> float` (bottom-left origin) |
| `mouseDeltaX` / `mouseDeltaY` | `() -> float`, reset on read |
| `mouseScrollX` / `mouseScrollY` | `() -> float`, reset on read |
| `mouseLeft` / `mouseRight` / `mouseMiddle` | `() -> bool` |
| `mouseButtonDown` / `mouseButtonPressed` / `mouseButtonReleased` | `(str button) -> bool` |
| `mouseButtonCode` | `(str button) -> int` |

## Sprites

Sprites are referenced by an integer index (`-1` means "not available").
`setSpriteVelocity` is in **pixels per second**; `updateSprite` applies the last
frame's `deltaTime()`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `makeSolidSprite` | `(int w, int h, r, g, b, float x, float y) -> int` | Coloured rectangle sprite. |
| `loadSprite` | `(str path, float scale, float x, float y) -> int` | Image sprite. |
| `setSpriteTexture` | `(int idx, int texIdx)` | Apply a cached texture. |
| `getSpriteX` / `getSpriteY` | `(int idx) -> float` | Position. |
| `getSpriteAngle` / `getSpriteScale` | `(int idx) -> float` | Rotation / scale. |
| `getSpriteWidth` / `getSpriteHeight` | `(int idx) -> float` | Scaled size. |
| `getSpriteVX` / `getSpriteVY` | `(int idx) -> float` | Velocity (px/s). |
| `getSpriteAngularVelocity` | `(int idx) -> float` | Degrees per second. |
| `getSpriteAlpha` | `(int idx) -> int` | Opacity 0–255. |
| `getSpriteVisible` | `(int idx) -> bool` | Visibility. |
| `getSpritePosition` | `(int idx) -> str` | `"x,y"`. |
| `setSpritePos` / `setSpritePosition` | `(int idx, float x, float y)` | Move. |
| `setSpriteAngle` / `setSpriteScale` | `(int idx, float value)` | Transform. |
| `setSpriteVelocity` | `(int idx, float vx, float vy)` | Velocity in px/s. |
| `setSpriteAngularVelocity` | `(int idx, float degreesPerSecond)` | Spin. |
| `stopSprite` | `(int idx)` | Zero velocity. |
| `moveSpriteToward` | `(int idx, float tx, float ty, float speed)` | Aim velocity at a point. |
| `faceSpriteTo` | `(int idx, float tx, float ty)` | Aim rotation at a point. |
| `setSpriteAlpha` | `(int idx, int alpha)` | Opacity 0–255. |
| `setSpriteColor` | `(int idx, r, g, b, a)` | Tint. |
| `setSpriteVisible` | `(int idx, bool visible)` | Show/hide. |
| `flipSpriteH` / `flipSpriteV` | `(int idx)` | Mirror. |
| `destroySprite` | `(int idx)` | Remove from registries and lists. |
| `spriteExists` | `(int idx) -> bool` | Live check. |
| `updateSprite` | `(int idx)` | Apply velocity for one frame. |
| `drawSprite` | `(int idx)` | Draw. |
| `spriteCollides` | `(int a, int b) -> bool` | Oriented bounding-box overlap. |
| `spriteCollidesWithList` | `(int spr, int list) -> bool` | Oriented overlap with any list member. |
| `getCollidingSprites` | `(int spr, int list) -> str` | `"[1,2]"`. |
| `spriteDistance` | `(int a, int b) -> float` | Centre distance. |
| `spriteNear` | `(int idx, float tx, float ty, float range) -> bool` | Within range. |

## Sprite lists

| Function | Signature |
|----------|-----------|
| `makeSpriteList` | `() -> int` |
| `addToList` | `(int list, int sprite)` |
| `removeSpriteFromList` | `(int list, int sprite)` |
| `clearSpriteList` | `(int list)` |
| `getSpriteListCount` | `(int list) -> int` |
| `drawSpriteList` | `(int list)` |
| `updateSpriteList` | `(int list)` |

## Textures

| Function | Signature | Description |
|----------|-----------|-------------|
| `loadTexture` | `(str path) -> int` | Cache and return a texture index. |
| `drawTexture` | `(int tex, float cx, float cy, float w, float h, float angle)` | Draw a cached texture. |
| `drawTextureAt` | `(str path, float cx, float cy, float scale)` | Load and draw directly. |
| `drawTextureRect` | `(int tex, float left, float bottom, float w, float h)` | Draw into a rectangle. |

## Camera

The camera is a transform applied by the draw calls.

| Function | Signature |
|----------|-----------|
| `makeCamera` | `() -> int` |
| `useCamera` | `(int cam)` |
| `setCameraPos` | `(int cam, float x, float y)` |
| `getCameraX` / `getCameraY` | `(int cam) -> float` |
| `zoomCamera` / `getCameraZoom` | `(int cam, float zoom)` / `(int cam) -> float` |
| `smoothScrollCamera` | `(int cam, float tx, float ty, float speed)` |
| `resetCamera` | `()` |

## Grid helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `screenToTile` | `(float x, float y, int tileSize) -> str` | Pixel → `"tx,ty"`. |
| `tileToScreen` | `(int tx, int ty, int tileSize) -> str` | Tile → `"sx,sy"` centre. |

---

## Scenes

A scene is a named collection of sprite lists that are drawn and updated
together.

| Function | Description |
|----------|-------------|
| `makeScene()` | Empty scene; returns its index |
| `addListToScene(sceneIdx, listIdx, name)` | Add (or replace) a named list |
| `drawScene(sceneIdx)` | Draw every list in the scene |
| `updateScene(sceneIdx)` | Apply each sprite's velocity for one frame |

## Text labels

| Function | Description |
|----------|-------------|
| `makeTextLabel(text, x, y, r, g, b, size, anchorX)` | Create a label; `anchorX` is `left`, `center` or `right` |
| `setTextLabel(idx, text)` / `setTextLabelPos(idx, x, y)` / `setTextLabelColor(idx, r, g, b, a)` | Change the label |
| `drawTextLabel(idx)` | Draw it during `on_draw` |
| `destroyTextLabel(idx)` | Release the slot |

## Tilemap

`loadTilemap(tmxPath, scaling)` reads a Tiled `.tmx` file and returns a **scene**
index; `getTilemapLayer(sceneIdx, layerName)` returns the named layer as a
**sprite list**. Inline and external TSX tilesets are supported, with global
tile IDs mapped to their respective atlas by `firstgid`, `columns`, `margin`
and `spacing`. CSV, XML `<tile gid>` and base64 layer data (raw, gzip or zlib)
are supported. Atlas and TSX image paths are resolved relative to their source
files; Tiled horizontal/vertical/diagonal flip flags are retained by tile
sprites.

Tiles become solid sprites on the grid, so a map carries geometry and collision
(hand a layer to `makePhysicsEngine` as the wall list). When the atlas cannot be
loaded or the module is headless, tiles fall back to shaded blocks.

## Physics

A small platformer engine: gravity, wall collision, slopes and one-way
platforms.

| Function | Description |
|----------|-------------|
| `makePhysicsEngine(gravity, wallsListIdx, oneWayListIdx = -1)` | Create an engine; pass `-1` for no walls or no one-way platforms |
| `setPhysicsPlayer(engineIdx, sprIdx)` | Assign the player sprite (required before updating) |
| `updatePhysics(engineIdx)` | One step at the current frame `dt` |
| `canJump(engineIdx)` | `true` while the player rests on a wall, a slope or a platform |
| `jumpPlayer(engineIdx, jumpSpeed)` | Set the player's Y velocity when grounded |
| `getPlayerVY(engineIdx)` | The player's current Y velocity |

A step moves the player by `vx`/`vy`, pushes them out of the side of a solid
wall, applies gravity, and then resolves the landing. Two conventions extend the
wall list:

- **A wall sprite with a non-zero `angle` is a slope.** Its surface runs from
  the box's bottom-left corner upward to the right by `tan(angle) * width`,
  never above the box's top. Slopes never block horizontal movement.
- **A sprite in the one-way list is a platform**, solid only for a player who
  was above it on the previous step and is falling now — so a jump carries the
  player up through it.

Sprite-to-sprite collision uses oriented bounding boxes based on each sprite's
angle. Physics accounts for the rotated player's bounds while preserving the
existing wall/slope rules. A grounded player is carried by the platform they
landed on when it translates or rotates between physics steps.

The player is snapped onto a slope or platform surface only from within two
units of it, which bounds how far a ramp can lift them in one step.

## Animation

| Function | Description |
|----------|-------------|
| `makeAnimatedSprite(pathsJson, fps, x, y)` | Sprite cycling through the images in a JSON array |
| `updateAnimation(idx, dt)` | Advance the frame clock |

Both are texture-backed, so in headless mode `makeAnimatedSprite` returns `-1`.

## Sound

| Function | Description |
|----------|-------------|
| `loadSound(path)` | Load an audio file; returns an index |
| `playSound(idx)` / `loopSound(idx)` | Play once / loop |
| `stopSound(idx)` | Stop playback |
| `setSoundVolume(idx, volume)` | Volume from `0.0` to `1.0` |
| `isSoundPlaying(idx)` | Playback state tracked by the module |

`isSoundPlaying` queries the rodio sink for current playback state, so completed
one-shots and stopped sounds return `false`, and a queued loop remains active
until stopped. `loadSound` returns `-1` in headless mode or when the file or
audio output device is unavailable.

## Notes and current limitations

The reference's shape batches, animated sprite-sheet helper, `setVSync` and
`getDisplaySize` are not exposed. `makeAnimatedSprite` accepts a JSON array of
separate image paths rather than a sprite sheet.

Other deviations:

- `setResizable` is accepted and ignored; `setTitle` applies when `run()` opens
  the window.
- `drawPolygon`/`drawPolyline`/`drawLines`/`drawPoints` take a flat coordinate
  string rather than JSON.
- `keyCode` returns this backend's key code, not Arcade's.
- `moveSpriteToward` sets velocity in px/s (the reference used px/frame).

## Rust / C ABI

The module is a single Rust `cdylib` (`rust/game`, crate `lynxer_game`); there
is no C++ shim. It exports `lynxer_module_init_v1`, every op as
`cdecl:<ret>(...)`, and `lynxer_module_attach_v1`. Macroquad owns the window and
event loop, and each frame it invokes the registered Lynxer callbacks through
the host API. It uses two additive native-module ABI extensions documented in
[`docs/native-module-abi.md`](../native-module-abi.md):

- the packed `...` signature, which passes any number of numeric/string
  arguments; and
- the optional `lynxer_module_attach_v1` entry point, which provides an
  `invoke` callback so a module can call a Lynxer function by name.

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
