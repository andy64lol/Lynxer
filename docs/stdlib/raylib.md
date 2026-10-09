# Raylib Module

The `raylib` module exposes (almost) every function of
[raylib](https://www.raylib.com/) 6.0 to Lynxer. It is a Rust backend that binds
`raylib-sys`, with the Lynxer-facing wrappers generated from raylib's own API
description.

> **Requires:** a Rust toolchain (`cargo`) to build the backend, and a display
> plus OpenGL to open a window. Everything that does not call `initWindow` —
> the math and color helpers, the enum constants, struct allocation and field
> access — works headless, which is what `stdlib_raylib.lynx` covers.

There are **1328 operations** (family functions, struct constructors/accessors
and enum/#define getters) and **308 constants**. The wrapper
`lynxer/stdlib/raylib.lynx` is generated; its one-line-per-op listing is the
complete reference. This page describes the shapes and the conventions.

## Naming and shapes

- Every raylib function keeps its name, lower-camel-cased:
  `DrawText` → `drawText`, `InitWindow` → `initWindow`, `GetRandomValue` →
  `getRandomValue`.
- raylib's enum values and `#define`s are exposed as **getter functions**, not
  constants: `flagWindowHidden()` is `128`, `keySpace()` is `32`,
  `mouseButtonLeft()` is `0`.
- `float` parameters and returns become Lynxer `float`; `int`/`bool` become
  `int`; `char*` becomes `str`.

### Scalar-only structs are flattened

Structs made only of scalars (`Vector2`/`Vector3`/`Vector4`, `Color`,
`Rectangle`, `Camera3D`, `Ray`, `BoundingBox`, `Texture`, `Matrix`, …) are
passed **by value as flattened scalars**, so a function taking one takes its
fields:

```lynx
// DrawRectangleRec(Rectangle, Color) becomes eight scalars.
global.raylib.drawRectangleRec(x, y, w, h, r, g, b, a);
```

A function returning one returns an **opaque `int` handle** you read with the
struct's field accessors and release with `<struct>Free`:

```lynx
int c = global.raylib.colorFromHSV(0.0, 1.0, 1.0); // Color handle
float red = global.raylib.colorGetR(c);            // 255.0
global.raylib.colorFree(c);
```

### Other structs are handles

Every other struct (`Image`, `Model`, `Sound`, `Music`, `Font`, `RenderTexture`,
`Shader`, `Mesh`, …) is an opaque `int` handle:

- `<struct>New(...)` allocates one (not all structs have a `New`);
- `<struct>Get<Field>(h)` / `<struct>Set<Field>(h, value)` read and write its
  scalar fields;
- `<struct>Free(h)` releases it (`h` of `0` is ignored).

## Handles are unvalidated pointers

A handle is the raw address of a heap-allocated value, and the accessors
dereference it directly. There is **no registry and no validation**, so passing
a wrong, stale or already-freed handle — or freeing one twice — is undefined
behaviour and will usually crash the process. Treat every handle as owned and
single-use, and `Free` it exactly once. This differs from most other modules,
which return a sentinel instead; see
[limitations.md](../limitations.md#raylib-handles-are-unvalidated).

## Representative operations

The table is illustrative, not exhaustive — see the generated wrapper for all
1328 names.

| Area | Examples |
| --- | --- |
| Window | `initWindow(w, h, title)`, `closeWindow()`, `windowShouldClose()`, `setTargetFPS(fps)`, `setWindowState(flags)` |
| Drawing | `beginDrawing()`, `endDrawing()`, `clearBackground(r, g, b, a)`, `drawPixel(x, y, r, g, b, a)`, `drawLine(...)`, `beginMode2D(...)`, `beginMode3D(...)` |
| Shapes | `drawRectangle(...)`, `drawRectangleRec(...)`, `drawCircle(...)`, `drawTriangle(...)`, `drawPoly(...)` |
| Text | `drawText(text, x, y, size, r, g, b, a)`, `measureText(text, size)`, `loadFont(path)`, `drawTextEx(font, text, …)` |
| Textures and images | `loadTexture(path)`, `loadImage(path)`, `loadRenderTexture(w, h)`, `imageResize(image, w, h)`, `updateTexture(texture, pixels)` |
| 3D | `drawCube(...)`, `loadModel(path)`, `drawModel(model, …)`, `loadShader(vs, fs)`, `updateCamera(camera)` |
| Audio | `initAudioDevice()`, `loadSound(path)`, `playSound(sound)`, `loadMusicStream(path)`, `updateMusicStream(music)` |
| Input | `isKeyDown(key)`, `isKeyPressed(key)`, `getMousePosition()`, `getMouseWheelMove()`, `isGamepadButtonDown(gamepad, button)` |
| Math and misc | `getRandomValue(min, max)`, `clamp(value, min, max)`, `lerp(start, end, amount)`, `getTime()`, `colorFromHSV(h, s, v)`, `fade(color, alpha)` |

## Example

```lynx
import("raylib")

global main(){
    global.raylib.initWindow(800, 450, "Lynxer + raylib");
    global.raylib.setTargetFPS(60);
    while(not global.raylib.windowShouldClose()){
        global.raylib.beginDrawing();
        global.raylib.clearBackground(245, 245, 245, 255);
        global.raylib.drawText("hello from Lynxer", 20, 20, 20, 0, 0, 0, 255);
        global.raylib.endDrawing();
    }
    global.raylib.closeWindow();
}
```

## Regenerating

The bindings are generated from raylib 6.0's API description, vendored so the
generator runs offline:

```console
$ python3 lynxer/rust/raylib/codegen/generate.py   # rewrites generated_ops.rs + raylib.lynx
$ make lynxer/stdlib/raylib.so
```

`generate.py` reads `codegen/raylib_api.json` (raylib's own `rlparser` output)
and writes both the Rust shims (`rust/raylib/src/generated_ops.rs`) and the
Lynxer wrapper (`stdlib/raylib.lynx`). Do not edit the generated files by hand.

## Not included

12 raylib functions are skipped because their shape cannot cross the
native-module ABI: `TraceLog`/`TextFormat` (variadic) and the `Set*Callback` /
`Attach`/`Detach*Processor` family (raw function pointers).
