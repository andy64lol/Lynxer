# image

Comprehensive image processing library for Lynxer, wrapping Python's [Pillow](https://pillow.readthedocs.io/) (PIL Fork). Load, transform, filter, draw on, composite, and export images — all referenced by integer indexes like tkinter widgets.

> **Requires:** `pip install Pillow`

---

## Quick start

```c
global setup(){ import("image"); }

global main(){
    int img = global.image.open("photo.png");
    print(global.image.info(img)); print("\n");  // {"width":800,"height":600,...}

    int gray   = global.image.grayscale(img);
    int blured = global.image.blur(gray, 3.0);
    global.image.save(blured, "output.png");
}
```

---

## Index model

Every function that creates an image returns a new integer **index** into an internal list (`builtins._lx_images`). The original is **never modified** unless stated otherwise (only `setPixel`, `setPixelA`, `fill`, `paste`, and `pasteWithAlpha` mutate in-place). Functions that return `-1` indicate an error (file not found, wrong mode, etc.).

---

## Core

| Function | Signature | Description |
|----------|-----------|-------------|
| `open` | `open(str path)` | Load image from file. Returns index. |
| `new` | `new(int width, int height, str mode, int r, int g, int b)` | Create blank image. `mode`: `"RGB"`, `"RGBA"`, `"L"`. Returns index. |
| `save` | `save(int idx, str path)` | Save image to file (format inferred from extension). |
| `saveQuality` | `saveQuality(int idx, str path, int quality)` | Save JPEG with quality 1–95. |
| `copy` | `copy(int idx)` | Deep-copy image. Returns new index. |
| `show` | `show(int idx)` | Open in system image viewer. |
| `close` | `close(int idx)` | Free image from memory. |
| `getWidth` | `getWidth(int idx)` | Width in pixels. |
| `getHeight` | `getHeight(int idx)` | Height in pixels. |
| `getMode` | `getMode(int idx)` | Color mode string: `"RGB"`, `"RGBA"`, `"L"`, etc. |
| `getFormat` | `getFormat(int idx)` | File format: `"PNG"`, `"JPEG"`, `""` if unknown. |
| `info` | `info(int idx)` | JSON string `{"width", "height", "mode", "format"}`. |

---

## Transform

All transform functions return a **new image index**.

| Function | Signature | Description |
|----------|-----------|-------------|
| `resize` | `resize(int idx, int width, int height)` | Resize to exact dimensions (Lanczos). |
| `thumbnail` | `thumbnail(int idx, int maxWidth, int maxHeight)` | Shrink to fit, preserve aspect ratio. |
| `scale` | `scale(int idx, float factor)` | Uniform scale (0.5 = half, 2.0 = double). |
| `fit` | `fit(int idx, int width, int height)` | Resize + center-crop to fill exactly. |
| `contain` | `contain(int idx, int width, int height, int r, int g, int b)` | Fit inside with padding. |
| `crop` | `crop(int idx, int x1, int y1, int x2, int y2)` | Crop to bounding box. |
| `rotate` | `rotate(int idx, float angle, bool expand)` | Rotate CCW; `expand=true` resizes canvas. |
| `flipH` | `flipH(int idx)` | Mirror left-right. |
| `flipV` | `flipV(int idx)` | Mirror top-bottom. |
| `pad` | `pad(int idx, int top, int right, int bottom, int left, int r, int g, int b)` | Add padding around image. |

---

## Convert

| Function | Signature | Description |
|----------|-----------|-------------|
| `convert` | `convert(int idx, str mode)` | Convert to any PIL mode (`"RGB"`, `"RGBA"`, `"L"`, `"P"`, `"CMYK"`, …). |
| `grayscale` | `grayscale(int idx)` | Convert to grayscale `"L"`. |
| `toRGB` | `toRGB(int idx)` | Remove alpha, force `"RGB"`. |
| `toRGBA` | `toRGBA(int idx)` | Add alpha channel, force `"RGBA"`. |
| `toBinary` | `toBinary(int idx, int threshold)` | 1-bit B&W; pixels > threshold → white. |

---

## Enhance

All return a **new image index**. Factor `1.0` = original.

| Function | Signature | Description |
|----------|-----------|-------------|
| `brightness` | `brightness(int idx, float factor)` | `0.0` = black, `1.0` = original, `2.0` = double bright. |
| `contrast` | `contrast(int idx, float factor)` | `0.0` = grey, `1.0` = original. |
| `color` | `color(int idx, float factor)` | `0.0` = grayscale, `1.0` = original, `2.0` = vivid. |
| `sharpness` | `sharpness(int idx, float factor)` | `0.0` = blurred, `1.0` = original, `2.0` = sharp. |
| `invert` | `invert(int idx)` | Photo-negative (inverts all pixel values). |

---

## Filter

All return a **new image index**.

| Function | Signature | Description |
|----------|-----------|-------------|
| `blur` | `blur(int idx, float radius)` | Gaussian blur. Higher radius = blurrier. |
| `boxBlur` | `boxBlur(int idx, float radius)` | Fast box blur. |
| `unsharpMask` | `unsharpMask(int idx, float radius, float percent, int threshold)` | Sharpen via blur subtraction. |
| `sharpen` | `sharpen(int idx)` | Standard sharpening kernel. |
| `edgeEnhance` | `edgeEnhance(int idx)` | Accentuate edges. |
| `emboss` | `emboss(int idx)` | 3-D emboss effect. |
| `findEdges` | `findEdges(int idx)` | Sketch-like edge detection. |
| `smooth` | `smooth(int idx)` | Mild smoothing (noise reduction). |
| `contour` | `contour(int idx)` | Outline-only contour drawing. |
| `detail` | `detail(int idx)` | Enhance fine details. |
| `medianFilter` | `medianFilter(int idx, int size)` | Noise removal (size must be odd). |
| `minFilter` | `minFilter(int idx, int size)` | Erosion-style filter. |
| `maxFilter` | `maxFilter(int idx, int size)` | Dilation-style filter. |

---

## Pixel operations

These mutate the image **in-place** (no new index returned for set calls).

| Function | Signature | Description |
|----------|-----------|-------------|
| `getPixel` | `getPixel(int idx, int x, int y)` | Returns `"r,g,b"` / `"r,g,b,a"` / `"v"`. |
| `setPixel` | `setPixel(int idx, int x, int y, int r, int g, int b)` | Set RGB pixel (in-place). |
| `setPixelA` | `setPixelA(int idx, int x, int y, int r, int g, int b, int a)` | Set RGBA pixel (in-place). |
| `fill` | `fill(int idx, int r, int g, int b)` | Fill entire image with solid color (in-place). |

---

## Composite

| Function | Signature | Description |
|----------|-----------|-------------|
| `paste` | `paste(int dstIdx, int srcIdx, int x, int y)` | Paste src onto dst at (x, y) — **in-place** on dst. |
| `pasteWithAlpha` | `pasteWithAlpha(int dstIdx, int srcIdx, int x, int y)` | Paste using src's alpha channel as mask — **in-place** on dst. |
| `blend` | `blend(int idx1, int idx2, float alpha)` | Linear blend: `img1*(1-α) + img2*α`. Returns new index. |
| `composite` | `composite(int baseIdx, int overlayIdx, int maskIdx)` | Composite overlay over base using L-mode mask. Returns new index. |
| `addAlpha` | `addAlpha(int idx)` | Add fully-opaque alpha channel (→ RGBA). Returns new index. |
| `setAlpha` | `setAlpha(int idx, int alpha)` | Set uniform alpha 0–255. Returns new index. |
| `removeAlpha` | `removeAlpha(int idx)` | Flatten RGBA → RGB on white background. Returns new index. |

---

## Drawing

All drawing functions work **in-place** on the image at `idx` using `ImageDraw`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `drawLine` | `drawLine(int idx, int x1, int y1, int x2, int y2, int r, int g, int b, int lineWidth)` | Line segment. |
| `drawRect` | `drawRect(int idx, int x1, int y1, int x2, int y2, int r, int g, int b, bool fill)` | Rectangle — filled or outline. |
| `drawRoundedRect` | `drawRoundedRect(int idx, int x1, int y1, int x2, int y2, int r, int g, int b, bool fill, int radius)` | Rounded rectangle. |
| `drawCircle` | `drawCircle(int idx, int cx, int cy, int radius, int r, int g, int b, bool fill)` | Circle centered at (cx, cy). |
| `drawEllipse` | `drawEllipse(int idx, int x1, int y1, int x2, int y2, int r, int g, int b, bool fill)` | Ellipse in bounding box. |
| `drawPolygon` | `drawPolygon(int idx, str coordsJson, int r, int g, int b)` | Filled polygon — `coordsJson`: flat `[x,y,…]` JSON. |
| `drawText` | `drawText(int idx, int x, int y, str text, int r, int g, int b, int size)` | Text using default/system font. |
| `drawTextFont` | `drawTextFont(int idx, int x, int y, str text, int r, int g, int b, int size, str fontPath)` | Text using a TTF/OTF file. |
| `floodFill` | `floodFill(int idx, int x, int y, int r, int g, int b)` | Flood-fill from seed point. |

---

## Analysis

| Function | Signature | Description |
|----------|-----------|-------------|
| `getAverageColor` | `getAverageColor(int idx)` | Average pixel color as `"r,g,b"`. |
| `getDominantColor` | `getDominantColor(int idx)` | Most common color (quantize to 1) as `"r,g,b"`. |
| `getHistogram` | `getHistogram(int idx)` | Pixel-value histogram as JSON array (256 per channel for RGB). |

---

## Special operations

All return a **new image index**.

| Function | Signature | Description |
|----------|-----------|-------------|
| `autoContrast` | `autoContrast(int idx)` | Stretch histogram to full range. |
| `equalize` | `equalize(int idx)` | Equalize global histogram. |
| `quantize` | `quantize(int idx, int colors)` | Reduce palette to N colors. |
| `solarize` | `solarize(int idx, int threshold)` | Invert pixels above threshold. |
| `posterize` | `posterize(int idx, int bits)` | Reduce each channel to N bits (1–8). |
| `tile` | `tile(int idx, int cols, int rows)` | Repeat image in a cols×rows grid. |
| `splitChannels` | `splitChannels(int idx)` | Split RGBA → 4 L-mode images. Returns `"ri,gi,bi,ai"` index string. |
| `mergeChannels` | `mergeChannels(int idxR, int idxG, int idxB, int idxA)` | Merge 4 L-mode images into RGBA. |

---

## Base64 / encoding

| Function | Signature | Description |
|----------|-----------|-------------|
| `toBase64` | `toBase64(int idx, str format)` | Encode to Base64 string. `format`: `"PNG"`, `"JPEG"`, `"WEBP"`, … |
| `fromBase64` | `fromBase64(str b64str)` | Decode Base64 back to image. Returns new index. |
| `toDataUrl` | `toDataUrl(int idx, str format)` | Data URL for HTML embedding: `data:image/png;base64,…` |

---

## Examples

### Resize and watermark

```c
global setup(){ import("image"); }

global main(){
    int img  = global.image.open("input.jpg");
    int sm   = global.image.resize(img, 640, 480);
    global.image.drawText(sm, 10, 10, "© 2026 Lynxer",
                          255, 255, 255, 18);
    global.image.save(sm, "output.jpg");
}
```

### Thumbnail strip

```c
global setup(){ import("image"); }

global main(){
    int a    = global.image.open("a.png");
    int b    = global.image.open("b.png");
    int ta   = global.image.thumbnail(a, 200, 200);
    int tb   = global.image.thumbnail(b, 200, 200);
    int strip = global.image.new(400, 200, "RGB", 30, 30, 30);
    global.image.paste(strip, ta, 0, 0);
    global.image.paste(strip, tb, 200, 0);
    global.image.save(strip, "strip.png");
}
```

### Filter pipeline

```c
global setup(){ import("image"); }

global main(){
    int src   = global.image.open("photo.jpg");
    int gray  = global.image.grayscale(src);
    int edges = global.image.findEdges(gray);
    int inv   = global.image.invert(edges);
    global.image.save(inv, "sketch.png");
}
```

### Base64 round-trip

```c
global setup(){ import("image"); }

global main(){
    int img = global.image.open("icon.png");
    str b64 = global.image.toBase64(img, "PNG");
    str url = global.image.toDataUrl(img, "PNG");
    print(url); print("\n");

    int restored = global.image.fromBase64(b64);
    global.image.save(restored, "restored.png");
}
```

### Drawing

```c
global setup(){ import("image"); }

global main(){
    int canvas = global.image.new(400, 300, "RGB", 255, 255, 255);
    global.image.drawRect(canvas, 20, 20, 380, 280, 30, 30, 30, false);
    global.image.drawCircle(canvas, 200, 150, 80, 100, 180, 255, true);
    global.image.drawText(canvas, 130, 240, "Hello, Pillow!", 0, 0, 0, 20);
    global.image.save(canvas, "drawing.png");
}
```
