# game

2-D game development toolkit for Lynxer, wrapping Python's [Arcade](https://api.arcade.academy/) library. Provides a window, drawing primitives, sprites, input polling, sound, scenes, tilemaps, camera, physics, and more — all accessible via Lynxer `rawPy` callbacks for the game loop.

> **Requires:** `pip install arcade`

---

## Quick start

```c
global setup(){ import("game"); }

global main(){
    global.game.init("My Game", 800, 600);
    global.game.setBackground(30, 30, 46);
    global.game.setFPSCap(60);

    rawPy(){
        import builtins
        px = [400.0]
        def _draw():
            global.game.beginDraw()
            global.game.drawCircle(px[0], 300, 40, 100, 200, 255)
            global.game.drawText("Arrow keys to move", 260, 20, 200, 200, 200, 16)
            global.game.endDraw()
        def _update(dt):
            if global.game.keyDown("LEFT"):  px[0] -= 200 * dt
            if global.game.keyDown("RIGHT"): px[0] += 200 * dt
        builtins._lx_game_win.on_draw   = _draw
        builtins._lx_game_win.on_update = _update
    }

    global.game.run();
}
```

The draw/update loop runs inside `rawPy` functions assigned to `builtins._lx_game_win.on_draw` and `builtins._lx_game_win.on_update`. All `global.game.*` functions can be called freely inside those Python functions.

---

## Window

| Function | Signature | Description |
|----------|-----------|-------------|
| `init` | `init(str title, int width, int height)` | Create the arcade window. Must be called first. |
| `setTitle` | `setTitle(str title)` | Change window title. |
| `setBackground` | `setBackground(int r, int g, int b)` | Clear color (0–255 per channel). |
| `getWidth` | `getWidth()` | Window width in pixels. |
| `getHeight` | `getHeight()` | Window height in pixels. |
| `setWindowSize` | `setWindowSize(int width, int height)` | Resize the window. |
| `setResizable` | `setResizable(bool enabled)` | Allow or prevent interactive resizing. |
| `setMouseVisible` | `setMouseVisible(bool visible)` | Show or hide the mouse cursor. |
| `setFPSCap` | `setFPSCap(int fps)` | Set target frame rate. |
| `getFPS` | `getFPS()` | Current frames per second. |
| `setFullscreen` | `setFullscreen(bool enabled)` | Enter or leave fullscreen. |
| `setWindowPos` | `setWindowPos(int x, int y)` | Set window position on screen. |
| `hideCursor` | `hideCursor()` | Hide mouse cursor over window. |
| `showCursor` | `showCursor()` | Show mouse cursor. |
| `screenshot` | `screenshot(str path)` | Save the current frame to a PNG. |
| `close` | `close()` | Close the window. |

---

## App lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `run` | `run()` | Start the arcade event loop (blocking). Call last. |

---

## Draw loop

Call inside `on_draw`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `beginDraw` | `beginDraw()` | Clear screen to background color. |
| `endDraw` | `endDraw()` | No-op — kept for symmetry. |

---

## Shape drawing

All colors are `int r, g, b` (0–255). All coordinates use arcade's bottom-left origin (Y increases upward).

### Filled shapes

| Function | Signature | Description |
|----------|-----------|-------------|
| `drawRect` | `drawRect(float cx, float cy, float w, float h, int r, int g, int b)` | Filled rectangle. |
| `drawRectRoundedFilled` | `drawRectRoundedFilled(float cx, float cy, float w, float h, int r, int g, int b, float cr)` | Filled rounded rectangle (corner radius `cr`). |
| `drawCircle` | `drawCircle(float cx, float cy, float radius, int r, int g, int b)` | Filled circle. |
| `drawEllipse` | `drawEllipse(float cx, float cy, float w, float h, int r, int g, int b)` | Filled ellipse. |
| `drawTriangle` | `drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3, int r, int g, int b)` | Filled triangle. |
| `drawPolygon` | `drawPolygon(str coordsJson, int r, int g, int b)` | Filled polygon — flat `[x,y,…]` JSON. |
| `drawArcFilled` | `drawArcFilled(float cx, float cy, float w, float h, int r, int g, int b, float startAngle, float endAngle)` | Filled pie sector. |
| `drawStar` | `drawStar(float cx, float cy, float outerR, float innerR, int n, int r, int g, int b)` | Filled n-point star. |
| `drawGradientRect` | `drawGradientRect(float cx, float cy, float w, float h, int r1, int g1, int b1, int r2, int g2, int b2)` | Simple two-color vertical gradient rect. |

### Outlines

| Function | Signature | Description |
|----------|-----------|-------------|
| `drawRectOutline` | `drawRectOutline(float cx, float cy, float w, float h, int r, int g, int b, float lw)` | Rectangle outline. |
| `drawRectRoundedOutline` | `drawRectRoundedOutline(float cx, float cy, float w, float h, int r, int g, int b, float cr, float lw)` | Rounded rectangle outline. |
| `drawCircleOutline` | `drawCircleOutline(float cx, float cy, float radius, int r, int g, int b, float lw)` | Circle outline. |
| `drawEllipseOutline` | `drawEllipseOutline(float cx, float cy, float w, float h, int r, int g, int b, float lw)` | Ellipse outline. |
| `drawTriangleOutline` | `drawTriangleOutline(float x1, float y1, float x2, float y2, float x3, float y3, int r, int g, int b, float lw)` | Triangle outline. |
| `drawArc` | `drawArc(float cx, float cy, float w, float h, int r, int g, int b, float startAngle, float endAngle, float lw)` | Arc outline. |

### Lines & points

| Function | Signature | Description |
|----------|-----------|-------------|
| `drawLine` | `drawLine(float x1, float y1, float x2, float y2, int r, int g, int b, float lw)` | Solid line. |
| `drawDashedLine` | `drawDashedLine(float x1, float y1, float x2, float y2, int r, int g, int b, float lw, float dashLen)` | Dashed line. |
| `drawPolyline` | `drawPolyline(str coordsJson, int r, int g, int b, float lw)` | Open polyline through multiple points. |
| `drawCross` | `drawCross(float cx, float cy, float size, int r, int g, int b, float lw)` | Plus/cross shape. |
| `drawPoint` | `drawPoint(float x, float y, int r, int g, int b, float size)` | Single point. |
| `drawPoints` | `drawPoints(str coordsJson, int r, int g, int b, float size)` | Many points — flat `[x,y,…]` JSON. |

---

## Text

| Function | Signature | Description |
|----------|-----------|-------------|
| `drawText` | `drawText(str text, float x, float y, int r, int g, int b, int size)` | Text at (x, y). |
| `drawTextStyled` | `drawTextStyled(str text, float x, float y, int r, int g, int b, int size, str fontName, bool bold, bool italic, str anchorX)` | Styled text. `anchorX`: `"left"`, `"center"`, `"right"`. |

---

## Texture / image drawing

| Function | Signature | Description |
|----------|-----------|-------------|
| `loadTexture` | `loadTexture(str imagePath)` | Load and cache a texture. Returns texture index. |
| `drawTexture` | `drawTexture(int texIdx, float cx, float cy, float w, float h, float angle)` | Draw cached texture at (cx, cy). |
| `drawTextureAt` | `drawTextureAt(str imagePath, float cx, float cy, float scale)` | Draw image file directly (no cache). |

---

## Sprites

| Function | Signature | Description |
|----------|-----------|-------------|
| `makeSolidSprite` | `makeSolidSprite(int width, int height, int r, int g, int b, float x, float y)` | Create a colored rectangle sprite. Returns an index. |
| `setSpriteTexture` | `setSpriteTexture(int idx, int texIdx)` | Apply a texture returned by `loadTexture`. |

### Sprite state

| Function | Signature | Description |
|----------|-----------|-------------|
| `getSpriteAlpha` | `getSpriteAlpha(int idx)` | Return opacity from 0 to 255. |
| `getSpriteScale` | `getSpriteScale(int idx)` | Return the current scale. |

Existing image sprites are loaded with `loadSprite`.

Sprites are referenced by integer indexes. All sprite operations use `builtins._lx_sprites[idx]` internally.

### Loading

| Function | Signature | Description |
|----------|-----------|-------------|
| `loadSprite` | `loadSprite(str path, float scale, float x, float y)` | Load image as sprite. Returns index (`-1` on error). |
| `makeAnimatedSprite` | `makeAnimatedSprite(str pathsJson, float fps, float x, float y)` | Animated sprite cycling through image files. `pathsJson` = JSON array of paths. |

### Position & transform

| Function | Signature | Description |
|----------|-----------|-------------|
| `getSpriteX` | `getSpriteX(int idx)` | Center X. |
| `getSpriteY` | `getSpriteY(int idx)` | Center Y. |
| `getSpriteAngle` | `getSpriteAngle(int idx)` | Rotation angle (degrees CCW). |
| `getSpriteWidth` | `getSpriteWidth(int idx)` | Sprite width in pixels. |
| `getSpriteHeight` | `getSpriteHeight(int idx)` | Sprite height in pixels. |
| `getSpriteVX` | `getSpriteVX(int idx)` | Velocity X (px/s). |
| `getSpriteVY` | `getSpriteVY(int idx)` | Velocity Y (px/s). |
| `setSpritePos` | `setSpritePos(int idx, float x, float y)` | Move sprite. |
| `setSpriteAngle` | `setSpriteAngle(int idx, float angle)` | Rotate sprite. |
| `setSpriteScale` | `setSpriteScale(int idx, float scale)` | Scale sprite. |
| `setSpriteVelocity` | `setSpriteVelocity(int idx, float vx, float vy)` | Set velocity in px/s. |
| `stopSprite` | `stopSprite(int idx)` | Zero velocity. |
| `moveSpriteToward` | `moveSpriteToward(int idx, float tx, float ty, float speed)` | Move sprite toward target at speed px/frame. |
| `faceSpriteTo` | `faceSpriteTo(int idx, float tx, float ty)` | Rotate sprite to face target point. |

### Appearance

| Function | Signature | Description |
|----------|-----------|-------------|
| `destroySprite` | `destroySprite(int idx)` | Remove a sprite from every list and release its registry slot. |
| `spriteExists` | `spriteExists(int idx)` | Check whether an index still refers to a live sprite. |
| `setSpriteAlpha` | `setSpriteAlpha(int idx, int alpha)` | Opacity 0–255. |
| `setSpriteColor` | `setSpriteColor(int idx, int r, int g, int b, int a)` | Tint color. |
| `flipSpriteH` | `flipSpriteH(int idx)` | Mirror horizontally. |
| `flipSpriteV` | `flipSpriteV(int idx)` | Mirror vertically. |
| `setSpriteVisible` | `setSpriteVisible(int idx, bool visible)` | Show or hide. |
| `getSpriteVisible` | `getSpriteVisible(int idx)` | `true` if visible. |

### Collision & distance

| Function | Signature | Description |
|----------|-----------|-------------|
| `spriteCollides` | `spriteCollides(int idxA, int idxB)` | `true` if two sprites overlap. |
| `spriteCollidesWithList` | `spriteCollidesWithList(int sprIdx, int listIdx)` | `true` if sprite hits any sprite in a list. |
| `getCollidingSprites` | `getCollidingSprites(int sprIdx, int listIdx)` | JSON array of colliding sprite indexes. |
| `spriteDistance` | `spriteDistance(int idxA, int idxB)` | Euclidean distance between two sprites. |
| `spriteNear` | `spriteNear(int idx, float tx, float ty, float range)` | `true` if sprite is within range pixels of (tx, ty). |

### Draw & update

| Function | Signature | Description |
|----------|-----------|-------------|
| `drawSprite` | `drawSprite(int idx)` | Draw single sprite. |
| `updateSprite` | `updateSprite(int idx)` | Apply velocity for one frame. |
| `updateAnimation` | `updateAnimation(int idx, float dt)` | Advance animated sprite by `dt` seconds. |

---

## Sprite lists

| Function | Signature | Description |
|----------|-----------|-------------|
| `makeSpriteList` | `makeSpriteList()` | Create empty `SpriteList`. Returns index. |
| `addToList` | `addToList(int listIdx, int sprIdx)` | Add sprite to list. |
| `removeSpriteFromList` | `removeSpriteFromList(int listIdx, int sprIdx)` | Remove sprite from list (doesn't destroy). |
| `clearSpriteList` | `clearSpriteList(int listIdx)` | Remove all sprites from list. |
| `getSpriteListCount` | `getSpriteListCount(int listIdx)` | Number of sprites in list. |
| `drawSpriteList` | `drawSpriteList(int listIdx)` | Draw all sprites. |
| `updateSpriteList` | `updateSpriteList(int listIdx)` | Apply velocities for all sprites. |

---

## Text labels

For HUDs and frequently changing text, use an Arcade text object instead of
recreating a draw call every frame:

| Function | Signature | Description |
|----------|-----------|-------------|
| `makeTextLabel` | `makeTextLabel(str text, float x, float y, int r, int g, int b, int size, str anchorX)` | Create a label and return its index. |
| `setTextLabel` | `setTextLabel(int idx, str text)` | Change label text. |
| `setTextLabelPos` | `setTextLabelPos(int idx, float x, float y)` | Move a label. |
| `setTextLabelColor` | `setTextLabelColor(int idx, int r, int g, int b, int a)` | Change label color. |
| `drawTextLabel` | `drawTextLabel(int idx)` | Draw the label during `on_draw`. |
| `destroyTextLabel` | `destroyTextLabel(int idx)` | Release the label registry slot. |

---

## Scene management

A **Scene** is a named collection of SpriteLists that can be drawn and updated together.

| Function | Signature | Description |
|----------|-----------|-------------|
| `makeScene` | `makeScene()` | Create empty scene. Returns index. |
| `addListToScene` | `addListToScene(int sceneIdx, int listIdx, str name)` | Add a SpriteList under a name. |
| `drawScene` | `drawScene(int sceneIdx)` | Draw all lists in the scene. |
| `updateScene` | `updateScene(int sceneIdx)` | Update all lists. |

---

## Tilemap (Tiled editor)

| Function | Signature | Description |
|----------|-----------|-------------|
| `loadTilemap` | `loadTilemap(str tmxPath, float scaling)` | Load a Tiled `.tmx` file. Returns **scene** index. |
| `getTilemapLayer` | `getTilemapLayer(int sceneIdx, str layerName)` | Get a named layer as a SpriteList index. |

```c
// Usage
int scene = global.game.loadTilemap("level1.tmx", 1.0);
int walls = global.game.getTilemapLayer(scene, "Walls");
```

---

## Input

### Keyboard

| Function | Signature | Description |
|----------|-----------|-------------|
| `keyDown` | `keyDown(str key)` | `true` while key is held. |
| `keyUp` | `keyUp(str key)` | `true` while key is NOT held. |
| `keyPressed` | `keyPressed(str key)` | Consume and return `true` once for a press event. |
| `keyReleased` | `keyReleased(str key)` | Consume and return `true` once for a release event. |

**Key names** (case-insensitive): `"UP"`, `"DOWN"`, `"LEFT"`, `"RIGHT"`, `"SPACE"`, `"ENTER"`, `"ESCAPE"`, `"A"`–`"Z"`, `"0"`–`"9"`, and `"F1"`–`"F12"`.

### Mouse

| Function | Signature | Description |
|----------|-----------|-------------|
| `mouseX` | `mouseX()` | X (pixels from left). |
| `mouseY` | `mouseY()` | Y (pixels from bottom). |
| `mouseDeltaX` | `mouseDeltaX()` | Accumulated horizontal movement since the previous query, then resets it. |
| `mouseDeltaY` | `mouseDeltaY()` | Accumulated vertical movement since the previous query, then resets it. |
| `mouseButtonDown` | `mouseButtonDown(str button)` | Generic query for `"LEFT"`, `"RIGHT"`, or `"MIDDLE"`. |
| `mouseLeft` | `mouseLeft()` | `true` if left button held. |
| `mouseRight` | `mouseRight()` | `true` if right button held. |
| `mouseMiddle` | `mouseMiddle()` | `true` if middle button held. |
| `mouseScrollY` | `mouseScrollY()` | Last scroll delta Y (positive = up). Resets after read. |

---

## Sound

| Function | Signature | Description |
|----------|-----------|-------------|
| `loadSound` | `loadSound(str path)` | Load audio file. Returns index. |
| `playSound` | `playSound(int soundIdx)` | Play (stops prior play of same sound). |
| `loopSound` | `loopSound(int soundIdx)` | Play on infinite loop. |
| `stopSound` | `stopSound(int soundIdx)` | Stop playback. |
| `setSoundVolume` | `setSoundVolume(int soundIdx, float volume)` | Volume 0.0–1.0. |
| `isSoundPlaying` | `isSoundPlaying(int soundIdx)` | `true` if currently playing. |

---

## Timer

| Function | Signature | Description |
|----------|-----------|-------------|
| `deltaTime` | `deltaTime()` | Seconds since last `on_update` call. |
| `getTime` | `getTime()` | Seconds elapsed since program start. |

---

## Camera

| Function | Signature | Description |
|----------|-----------|-------------|
| `makeCamera` | `makeCamera()` | Create `Camera2D`. Returns index. |
| `useCamera` | `useCamera(int camIdx)` | Activate camera for subsequent draws. |
| `setCameraPos` | `setCameraPos(int camIdx, float x, float y)` | Move camera center. |
| `getCameraX` | `getCameraX(int camIdx)` | Camera center X. |
| `getCameraY` | `getCameraY(int camIdx)` | Camera center Y. |
| `zoomCamera` | `zoomCamera(int camIdx, float zoom)` | Zoom level (1.0 = normal). |
| `smoothScrollCamera` | `smoothScrollCamera(int camIdx, float tx, float ty, float speed)` | Lerp camera toward target (`speed` 0.0–1.0). |
| `resetCamera` | `resetCamera()` | Restore full-window viewport. |

---

## Physics

Wraps `arcade.PhysicsEnginePlatformer`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `makePhysicsEngine` | `makePhysicsEngine(float gravity, int wallsListIdx)` | Create engine. Pass `-1` for no walls. Returns index. |
| `setPhysicsPlayer` | `setPhysicsPlayer(int engineIdx, int sprIdx)` | Assign player sprite (required before `updatePhysics`). |
| `updatePhysics` | `updatePhysics(int engineIdx)` | Step the engine (call in `on_update`). |
| `canJump` | `canJump(int engineIdx)` | `true` if player is on the ground. |
| `jumpPlayer` | `jumpPlayer(int engineIdx, float jumpSpeed)` | Apply upward impulse when on ground. |
| `getPlayerVY` | `getPlayerVY(int engineIdx)` | Player's current Y velocity. |

---

## Grid helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `screenToTile` | `screenToTile(float x, float y, int tileSize)` | Pixel → tile coords. Returns `"tx,ty"` string. |
| `tileToScreen` | `tileToScreen(int tx, int ty, int tileSize)` | Tile → pixel center. Returns `"sx,sy"` string. |

---

## Notes

- Arcade's coordinate origin `(0, 0)` is **bottom-left** (Y increases upward) — the opposite of most GUI toolkits.
- All draw calls must happen inside `on_draw`; calls outside have no visible effect.
- `deltaTime()` is automatically updated on every `on_update` call.
- For sprite-sheet animations or advanced shaders, use `rawPy` to access arcade directly via `builtins._lx_game_win`.

---

## Full example — platformer skeleton

```c
global setup(){ import("game"); }

global main(){
    global.game.init("Platformer", 800, 500);
    global.game.setBackground(100, 160, 220);
    global.game.setFPSCap(60);

    // Load tilemap (requires level.tmx from Tiled editor)
    // int scene = global.game.loadTilemap("level.tmx", 1.0);
    // int walls = global.game.getTilemapLayer(scene, "Walls");

    // Fallback: hand-built ground
    int walls  = global.game.makeSpriteList();
    int ground = global.game.loadSprite("ground.png", 1.0, 400, 16);
    global.game.addToList(walls, ground);

    int player = global.game.loadSprite("player.png", 0.5, 100, 100);
    global.game.setSpriteColor(player, 80, 180, 255, 255);

    int eng = global.game.makePhysicsEngine(0.6, walls);
    global.game.setPhysicsPlayer(eng, player);

    int cam = global.game.makeCamera();

    int snd = global.game.loadSound("jump.wav");

    rawPy(){
        import builtins

        def _draw():
            global.game.beginDraw()
            global.game.useCamera(cam)
            global.game.drawSpriteList(walls)
            global.game.drawSprite(player)
            global.game.resetCamera()
            global.game.drawText("WASD / Arrow keys to move", 10, 10,
                                  255, 255, 255, 14)
            fps = global.game.getFPS()
            global.game.drawText(f"FPS: {fps:.0f}", 10, 30, 200, 255, 200, 14)
            global.game.endDraw()

        def _update(dt):
            speed = 220 * dt
            if global.game.keyDown("LEFT") or global.game.keyDown("A"):
                global.game.setSpriteVelocity(player,
                    -220, global.game.getSpriteVY(eng))
            elif global.game.keyDown("RIGHT") or global.game.keyDown("D"):
                global.game.setSpriteVelocity(player,
                    220, global.game.getSpriteVY(eng))
            else:
                global.game.stopSprite(player)

            if (global.game.keyDown("UP") or global.game.keyDown("SPACE")) \
                    and global.game.canJump(eng):
                global.game.jumpPlayer(eng, 14)
                global.game.playSound(snd)

            global.game.updatePhysics(eng)

            // Smooth-scroll camera to follow player
            global.game.smoothScrollCamera(cam,
                global.game.getSpriteX(player),
                global.game.getSpriteY(player) + 60,
                0.1)

        builtins._lx_game_win.on_draw   = _draw
        builtins._lx_game_win.on_update = _update
    }

    global.game.run();
}
```
