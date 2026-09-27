# graphics

General-purpose drawing, window, input and immediate-mode UI toolkit, backed by
macroquad through the native module `stdlib/graphics.so` (Rust crate
`rust/graphics`).

> **Requires:** a Rust toolchain to build, and a display with OpenGL
> (X11 or Wayland) to run. Without one, set `LYNXER_GRAPHICS_HEADLESS=1`.
> Linux is the only supported platform.

`graphics` is the low-level surface. The [game](game.md) module is a separate,
game-oriented layer over the same backend (sprites, scenes, tilemaps, physics);
the two do not share state and can be imported together.

```lynx
global setup(){ import("graphics"); }

global onStart(){
    global.graphics.loadFont("assets/font.ttf");
}

global onUpdate(float dt){ }

global onDraw(){
    global.graphics.clearBackground(20, 20, 30, 255);
    global.graphics.drawCircle(160.0, 120.0, 40.0, 220, 80, 80);
    global.graphics.drawText("hello", 8.0, 24.0, 24.0, 255, 255, 255);
}

global main(){
    global.graphics.init("demo", 800, 600);
    global.graphics.setStartCallback("onStart");
    global.graphics.setUpdateCallback("onUpdate");
    global.graphics.setDrawCallback("onDraw");
    global.graphics.run();
}
```

## Coordinates and units

- Screen space, origin at the **top-left**, +Y pointing **down**.
- Colours are `0..255` per channel, alpha last. The shape helpers default alpha
  to `255`.
- `rotation` is in **radians** for the rotated-rectangle ops, textures and text,
  and in **degrees** for polygons, ellipses and arcs (macroquad's own split).
- Angles passed to `setCamera2D` are in degrees.

## Frames and callbacks

`run()` opens the window and blocks, invoking the registered callbacks once per
frame: `start` once, then `update(dt)` and `draw()`. Before `draw()`, the loop
clears the window to the colour set with `setBackground`. `stop()` ends the
loop; Ctrl-C exits with code 130.

Every op that touches the GPU context — `loadTexture`, `loadFont`,
`loadMaterial`, `renderTarget`, `getScreenData`, `screenshot`, all `draw*` ops
and all `ui*` widgets — is only valid **inside a callback**. That is when the
window's context exists. Calling them from `setup` or `main` will fail.

## Headless mode

`LYNXER_GRAPHICS_HEADLESS=1` runs without a window: `run()` executes three
frames at a fixed `1/60` timestep, drawing is a no-op, and ops that need a
context return a placeholder:

| Category | Headless result |
| --- | --- |
| `loadTexture`, `loadImage`, `loadFont`, `loadMaterial`, `renderTarget`, `renderTargetMsaa`, `screenshot`, `getScreenData` | `-1` |
| `textureWidth`, `textureHeight`, `measureWidth`, `measureHeight` | `0` |
| `measure`, `measureMultiline` | `{"width":0,"height":0,"offsetY":0}` |
| `screenToWorld`, `worldToScreen`, `textureSize`, `windowPosition`, `mousePosition` | `[0,0]` |
| `keysDown`, `keysPressed`, `keysReleased`, `touches`, `droppedFiles` | `[]` |
| every `isKey*` / `isMouse*` / `isRunning` / `quitRequested` | `false` |

Ops that need no context keep working: images built with `genImage` and their
pixels, `rgbToHsl`/`hslToRgb`, the key-name table, the 2D camera value, and the
UI value registry.

## Window and frame loop

| Function | Returns | Notes |
| --- | --- | --- |
| `init(title, width, height)` | — | before `run()`; resets the registries |
| `run()` | — | blocks; drives the callbacks |
| `stop()` | — | end the loop after the current frame |
| `isRunning()` | `bool` | |
| `screenWidth()`, `screenHeight()` | `float` | |
| `dpiScale()` | `float` | |
| `requestScreenSize(w, h)` | — | |
| `setWindowSize(w, h)`, `setWindowPosition(x, y)` | — | |
| `windowPosition()` | `str` | JSON `[x,y]` |
| `setResizable`, `setFullscreen`, `setHighDpi` | — | `(bool)` |
| `setSampleCount(n)` | — | MSAA samples |
| `setDefaultFilterMode(nearest)` | — | `true` = nearest, `false` = linear |
| `setTargetFps(fps)` | — | `0` disables the cap |
| `setBackground(r,g,b,a)` | — | the loop's clear colour |
| `setStartCallback` / `setUpdateCallback` / `setDrawCallback(name)` | — | |
| `showMouse(shown)`, `setCursorGrab(grab)` | — | |
| `setMouseCursor(kind)` | — | `0` default, `2` pointer, `4` crosshair, `5` text, `8`–`11` resizes |
| `clipboardGet()` / `setClipboard(text)` | `str` / — | |
| `quitRequested()`, `requestQuit()`, `preventQuit()` | `bool` / — / — | |
| `screenshot(path)` | `int` | `0` on success |
| `version()` | `str` | |

### Frame timing

`deltaTime() -> float`, `time() -> float` (since `run()`), `fps() -> int`, and
`drawFps(x, y)`.

## 2D shapes

Colours are `(r, g, b)` with an optional `a` defaulting to `255`.

| Function | Notes |
| --- | --- |
| `clearBackground(r,g,b,a)` | |
| `drawLine(x1,y1,x2,y2,thickness,...)` | |
| `drawTriangle(x1,y1,x2,y2,x3,y3,...)` | filled |
| `drawTriangleLines(x1,y1,x2,y2,x3,y3,thickness,...)` | outline |
| `drawRectangle(x,y,w,h,...)` | `x,y` is the top-left corner |
| `drawRectangleLines(x,y,w,h,thickness,...)` | |
| `drawRectangleRotated(x,y,w,h,rotation,originX,originY,...)` | pivot as a fraction (`0.5,0.5` = centre) |
| `drawRectangleLinesRotated(x,y,w,h,thickness,rotation,originX,originY,...)` | |
| `drawPoly(x,y,sides,radius,rotation,...)` | `sides` clamped to `3..255` |
| `drawPolyLines(x,y,sides,radius,rotation,thickness,...)` | |
| `drawCircle(x,y,radius,...)` | |
| `drawCircleLines(x,y,radius,thickness,...)` | |
| `drawEllipse(x,y,w,h,rotation,...)` | |
| `drawEllipseLines(x,y,w,h,rotation,thickness,...)` | |
| `drawArc(x,y,sides,radius,rotation,thickness,arc,...)` | `arc` is the sweep in degrees |
| `drawHexagon(x,y,size,border,vertical,...)` | border and fill share one colour |

## Text and fonts

| Function | Notes |
| --- | --- |
| `loadFont(path) -> int` | TTF/OTF; `-1` when unavailable |
| `setDefaultFont(font) -> int` | use a handle for `drawText` |
| `drawText(text,x,y,size,...)` | |
| `drawTextEx(font,text,x,y,size,scale,rotation,...)` | |
| `drawMultilineText(text,x,y,size,separation,...)` | wrapped |
| `drawMultilineTextEx(font,text,x,y,size,separation,scale,rotation,...)` | |
| `measure(text,font,size,scale) -> str` | JSON `{width,height,offsetY}` |
| `measureWidth` / `measureHeight(text,font,size,scale) -> float` | |
| `measureMultiline(text,font,size,maxWidth,scale) -> str` | |
| `textCenter(text,font,size,scale,rotation) -> str` | JSON `[x,y]` |
| `wrapText(text,font,size,maxWidth) -> str` | |

Pass `-1` as the font handle to use the built-in font. `font` and `scale` are
trailing defaults on `measure*` and `textCenter`.

## Cameras

| Function | Notes |
| --- | --- |
| `setCamera2D(targetX,targetY,zoom,rotation,offsetX,offsetY)` | rotation in degrees |
| `setCamera2DFromRect(x,y,w,h)` | show a world rectangle |
| `setDefaultCamera()` | back to screen space |
| `pushCameraState()` / `popCameraState()` | |
| `screenToWorld(x,y)` / `worldToScreen(x,y)` | JSON `[x,y]` |
| `setCamera3D(posX,posY,posZ,targetX,targetY,targetZ,upX,upY,upZ,fovy,orthographic,zNear,zFar)` | |
| `cameraTarget()` | JSON `[targetX,targetY,zoom,rotation,offsetX,offsetY]` |

## Textures, images and render targets

Handles are integers; `-1` means "unavailable".

| Function | Notes |
| --- | --- |
| `loadTexture(path) -> int` | GPU texture |
| `loadImage(path) -> int` | CPU image |
| `textureFromImage(image) -> int` | upload |
| `imageFromTexture(texture) -> int` | download |
| `genImage(w,h,r,g,b,a) -> int` | solid colour; works headless |
| `imageWidth` / `imageHeight(image) -> int` | |
| `imageGetPixel(image,x,y) -> str` | JSON `[r,g,b,a]` |
| `imageSetPixel(image,x,y,r,g,b,a) -> int` | |
| `exportImage(image, path) -> int` | PNG |
| `textureWidth` / `textureHeight(texture) -> float` | |
| `textureSize(texture) -> str` | JSON `[w,h]` |
| `drawTexture(texture,x,y,r,g,b,a)` | natural size |
| `drawTextureScaled(texture,x,y,w,h,r,g,b,a)` | |
| `drawTextureRegion(texture,x,y,w,h,sourceX,sourceY,sourceW,sourceH,r,g,b,a)` | |
| `drawTextureRotated(texture,x,y,w,h,rotation,r,g,b,a)` | around the centre |
| `setTextureFilter(texture, nearest)` | |
| `buildTexturesAtlas()` | |
| `renderTarget(w,h) -> int` / `renderTargetMsaa(w,h) -> int` | |
| `renderTargetTexture(target) -> int` | register it as a texture |
| `setRenderTarget(target) -> int` / `endRenderTarget()` | |
| `getScreenData() -> int` | this frame as an image |

## Input

Keys are named, not numeric. Names are case- and separator-insensitive:
`"space"`, `"Left Shift"`, `"f12"`, `"kpenter"`, `"leftcontrol"`. Aliases
include `esc`, `return`, `ctrl`, `shift`, `alt`, `del`, `pgup`, `pgdn` and the
`arrow*` spellings.

| Function | Returns |
| --- | --- |
| `isKeyDown` / `isKeyPressed` / `isKeyReleased(key)` | `bool` |
| `anyKeyDown()` | `bool` |
| `lastKeyPressed()` | `str` |
| `keysDown` / `keysPressed` / `keysReleased()` | JSON string array |
| `charPressed()` | `str` |
| `keyCode(key) -> int` / `keyName(code) -> str` | bridge to macroquad codes |
| `mousePosition` / `mousePositionLocal` / `mouseDelta` / `mouseWheel()` | JSON `[x,y]` |
| `isMouseButtonDown` / `Pressed` / `Released(button)` | `bool`; `0` left, `1` middle, `2` right |
| `touches()` | JSON `{id,x,y,phase}` array |
| `clearInputQueue()` | — |
| `droppedFiles()` | JSON path array |
| `simulateMouseWithTouch(enabled)` | — |

## 3D primitives

Use `setCamera3D` first. All take `(r, g, b)` with alpha defaulting to `255`.

`drawLine3D`, `drawCube`, `drawCubeWires`, `drawSphere`, `drawSphereWires`,
`drawSphereEx` (explicit rings/slices), `drawCylinder`, `drawCylinderWires`,
`drawCylinderEx` (explicit side count), `drawPlane`, `drawGrid`,
`drawAffineParallelogram` and `drawAffineParallelepiped`.

## Materials

| Function | Notes |
| --- | --- |
| `loadMaterial(vertexSource, fragmentSource) -> int` | GLSL |
| `loadMaterialFromFile(vertexPath, fragmentPath) -> int` | |
| `useMaterial(material) -> int` / `useDefaultMaterial()` | |
| `setUniform(material, name, value) -> int` | float |
| `setUniformArray(material, name, valuesJson) -> int` | JSON float array |
| `setMaterialTexture(material, name, texture) -> int` | |

## Immediate-mode UI

Widgets are addressed by an integer id. Values are held by the module, so they
survive across frames and can be read or written from Lynxer.

| Function | Notes |
| --- | --- |
| `uiLabel(text)` / `uiLabelAt(text,x,y)` | |
| `uiButton(id,text) -> bool` / `uiButtonAt(id,text,x,y) -> bool` | clicked this frame |
| `uiCheckbox(id,label) -> bool` | |
| `uiCheckboxValue(id) -> bool` / `uiSetCheckboxValue(id, value)` | |
| `uiSlider(id,label,min,max) -> float` | |
| `uiSliderValue(id) -> float` / `uiSetSliderValue(id, value)` | |
| `uiInputText(id,label) -> str` / `uiInputPassword(id,label) -> str` | |
| `uiInputTextValue(id) -> str` / `uiSetInputText(id, text)` | |
| `uiProgressBar(label, value, min, max)` | value normalised into `min..max` |
| `uiComboBox(id,label,optionsJson) -> int` | JSON string array; selected index |
| `uiComboBoxValue(id) -> str` | selected text |
| `uiSeparator()` / `uiSameLine(x)` | |
| `uiResult(id) -> int` | last button click / combo selection |
| `uiWindowBegin(id,title,x,y,w,h) -> int` / `uiWindowEnd() -> int` | draggable panel |
| `uiGroupBegin(id,w,h) -> int` / `uiGroupEnd() -> int` | layout group |

macroquad's window and group take a closure, which a flat op list cannot nest,
so `uiWindowBegin`/`uiGroupBegin` **buffer** the widgets issued until the
matching `*End`, then replay them inside that closure. One block deep is
supported: a `*Begin` while a block is open returns `-1`. Widget values update
when the block is replayed, so read them after `uiWindowEnd`/`uiGroupEnd`
rather than between the calls.

## Randomness and colour

`srand(seed)`, `rand() -> int`, `genRange(low,high) -> float`,
`genRangeInt(low,high) -> int`, `rgbToHsl(r,g,b) -> str` (JSON `[h,s,l]`),
`hslToRgb(h,s,l) -> str` (JSON `[r,g,b,a]`), and `setAssetsFolder(path)`.

## Notes and current limitations

- **A display is mandatory for real rendering.** macroquad has no offscreen
  backend, so anything that draws needs a window and OpenGL. Headless mode
  returns placeholders instead of rendering.
- **No audio.** macroquad's audio feature needs a crate that is not vendored;
  use the [sound](sound.md) module instead.
- **No native OS widgets.** The `ui*` set is drawn by macroquad onto the
  canvas; there is no GTK/Qt/Tk equivalent.
- **No gamepad input.** macroquad 0.4.16 has no gamepad API.
- **No 3D model loading.** macroquad can draw generated primitives but has no
  model importer, and `Mesh` cannot be built from Lynxer.
- **No window retitle, `setTargetFps` on the backend, or window-resize query.**
  miniquad 0.4.11 exposes none of them; the title is set once by `run()` and the
  frame rate is capped by sleeping in the loop.
- **The windowed path is not covered by CI.** CI runs with
  `LYNXER_SKIP_DISPLAY=1`, which skips the graphics fixture; the build, the
  module-contract check and the headless fixture are what gate it there.

## Rust / C ABI

The crate is `lynxer_graphics` under `rust/graphics`, a `cdylib` exporting
`lynxer_module_init_v1` and `lynxer_module_attach_v1`. Every op is registered
with the packed signature (`cdecl:int64(...)`, `cdecl:float64(...)` or
`cdecl:cstring(...)`) because several take more than the four arguments the
fixed grammar allows. See [native-module-abi.md](../native-module-abi.md).

## See also

- [game.md](game.md) — the game-oriented layer over the same backend.
- [native-module-abi.md](../native-module-abi.md) — writing a Rust backend.
- [limitations.md](../limitations.md) — platform constraints.
