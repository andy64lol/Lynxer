# image

Image loading, creation, transforms, pixels, and encoding. The backend is a
Rust `cdylib` built with the `image` crate and installed as
`stdlib/image.so`. Images are process-local integer handles; `-1` indicates a
failed operation.

> **Requires:** a Rust toolchain (`cargo`). Build it with `make cargo` or
> `make buildLynxer`. The module supports PNG, JPEG, GIF, BMP, TIFF, WebP, ICO,
> QOI, TGA, PNM, HDR, OpenEXR, and farbfeld, and rasterises SVG.

```lynx
global setup(){ import("image"); }

global main(){
    int picture = global.image.create(64, 64, "RGBA", 20, 30, 40);
    global.image.setPixel(picture, 0, 0, 255, 0, 0);
    println(global.image.getPixel(picture, 0, 0));
    println(global.image.getWidth(picture), "x", global.image.getHeight(picture));
    global.image.save(picture, "picture.png");
    global.image.close(picture);
}
```

## Handles and metadata

| Function | Description |
|---|---|
| `open(path)` | Decode an image file and return a handle. An `.svg`/`.svgz` path is rasterised. |
| `openSvg(path)` | Rasterise an SVG at its intrinsic size. |
| `openSvgSize(path, width, height)` | Rasterise an SVG scaled to fit inside `width`×`height`, preserving aspect ratio. |
| `create(width, height, mode, r, g, b)` | Create an image in `L`, `RGB`, or `RGBA` mode. |
| `copy(handle)` | Clone an image into a new handle. |
| `close(handle)` | Release a handle. |
| `getWidth` / `getHeight` | Read dimensions. |
| `getMode` / `getFormat` | Read the color mode and detected source format. |
| `info(handle)` | Return JSON metadata containing width, height, mode, and format. |
| `save(handle, path)` | Encode using the path extension. |
| `saveQuality(handle, path, quality)` | Save JPEG with quality from 1 to 95. |

### Formats

`open` and `save` handle PNG, JPEG, GIF, BMP, TIFF, WebP, ICO, QOI, TGA, PNM
(PBM/PGM/PPM/PAM), Radiance HDR, OpenEXR, and farbfeld. The format is chosen from
the file extension; `getFormat` reports the codec a handle was decoded with, and
`save` picks the encoder from the destination extension.

### SVG

SVG is vector, so it is rasterised rather than decoded through a codec.
`openSvg`/`openSvgSize` (and `open` on a `.svg`/`.svgz` path) produce an ordinary
`RGBA` handle: `getMode` returns `RGBA`, `getFormat` returns `SVG`, and every
transform, pixel, drawing and encoding function accepts it. Rasterising is
one-way — there is no SVG encoder, so `save(handle, "x.svg")` fails. Text uses
the host's system fonts, and relative `<image>` references resolve against the
SVG's own directory.

## Transforms and pixels

`resize`, `thumbnail`, `scale`, `crop`, `rotate`, `flipH`, `flipV`, `pad`,
`convert`, `grayscale`, `toRGB`, `toRGBA`, `toBinary`, `brightness`, `contrast`,
`invert`, `blur`, `boxBlur`, `unsharpMask`, and `sharpen` return new handles.

`getPixel`, `getAverageColor`, and `getDominantColor` return JSON arrays in
`[r,g,b,a]` order; histogram and info results are JSON objects. `getPixel`
returns `[]` for an invalid coordinate or handle. `setPixel`, `setPixelA`, and `fill`
mutate the existing image. `paste` and `pasteWithAlpha` mutate the destination;
`blend` returns a new image. `addAlpha`, `setAlpha`, and `removeAlpha` return
new handles.

## Geometry fitting

| Function | Description |
|---|---|
| `fit(idx, width, height)` | Resize to cover `width`×`height`, then centre-crop — no letterboxing. |
| `contain(idx, width, height, r, g, b)` | Scale to fit inside `width`×`height`, centred on a solid background. |

## Fixed and rank filters

These return a new handle. The 3×3 filters are Lynxer's own kernels with the
descriptions below; each divides the neighbourhood and leaves alpha untouched,
so a filter never changes transparency.

| Function | Kernel / window |
|---|---|
| `smooth(idx)` | `[1,1,1, 1,5,1, 1,1,1] / 13` — mild smoothing. |
| `detail(idx)` | `[0,-1,0, -1,10,-1, 0,-1,0]` — fine-detail emphasis. |
| `edgeEnhance(idx)` | `[-1,-1,-1, -1,9,-1, -1,-1,-1]` — edge accentuation. |
| `emboss(idx)` | 3×3 emboss kernel with a `+128` offset, so flat areas stay mid-grey. |
| `findEdges(idx)` | `[-1,-1,-1, -1,8,-1, -1,-1,-1]` — edge detection. |
| `contour(idx)` | `[1,1,1, 1,-7,1, 1,1,1]` — outline only. |
| `medianFilter(idx, size)` / `minFilter` / `maxFilter` | Rank filters over a `size`×`size` window (`size` is forced odd). Median removes noise; min erodes; max dilates. |

## Enhancement and composition

| Function | Description |
|---|---|
| `color(idx, factor)` | Blend towards grey: `0.0` is greyscale, `1.0` unchanged, `2.0` saturated. |
| `sharpness(idx, factor)` | Blend between a blurred copy and the original: `0.0` blur, `1.0` unchanged, `2.0` sharpened. |
| `composite(baseIdx, overlayIdx, maskIdx)` | Overlay where the `L`-mode mask is white, base where it is black. |

## Histogram operations

Each returns a new handle and leaves alpha alone.

| Function | Description |
|---|---|
| `autoContrast(idx)` | Stretch each colour channel's range across the full byte range. |
| `equalize(idx)` | Histogram equalisation, with the lowest populated level preserved. |
| `solarize(idx, threshold)` | Invert every channel at or above `threshold`. |
| `posterize(idx, bits)` | Keep the top `bits` bits (1–8) of each channel. |
| `quantize(idx, colors)` | Median-cut palette reduction to at most `colors` colours. |

## Channels

| Function | Description |
|---|---|
| `splitChannels(idx)` | Four `L` band images, returned as `"r,g,b,a"` indices. |
| `mergeChannels(idxR, idxG, idxB, idxA)` | Merge `L` bands back into `RGBA`; a missing or invalid `idxA` is fully opaque. |

## Drawing

These mutate the target handle and return `true`/`false`. Coordinates are
pixels; rectangles and ellipses accept either corner order. Colours are `r, g, b`.

| Function | Description |
|---|---|
| `drawLine(idx, x1, y1, x2, y2, r, g, b, lineWidth)` | Line segment with a square brush. |
| `drawRect(idx, x1, y1, x2, y2, r, g, b, fill)` | Rectangle, filled or outlined. |
| `drawRoundedRect(idx, x1, y1, x2, y2, r, g, b, fill, radius)` | Rounded rectangle. |
| `drawCircle(idx, cx, cy, radius, r, g, b, fill)` | Circle centred on `(cx, cy)`. |
| `drawEllipse(idx, x1, y1, x2, y2, r, g, b, fill)` | Ellipse inscribed in the box. |
| `drawPolygon(idx, coordsJson, r, g, b)` | Filled polygon from a flat `[x,y,x,y,…]` JSON array. |
| `drawText(idx, x, y, text, r, g, b, size)` | Text at `(x, y)` using the first available system font. |
| `drawTextFont(idx, x, y, text, r, g, b, size, fontPath)` | Text using a specific TTF/OTF file. |
| `floodFill(idx, x, y, r, g, b)` | Flood-fill the contiguous region under the seed point. |

Text is rasterised with `fontdue` and blended by glyph coverage, so edges show
the background through. `drawText` returns `false` when the host has no
discoverable font, and `drawTextFont` returns `false` for an unreadable path.
Both leave the image untouched on failure.

## Viewer

`show(idx)` writes the image to a temporary PNG and opens it with the system
viewer (`xdg-open`, or `open` on macOS). It returns `true`/`false`; it is not
usable headlessly.

## Analysis and encoding

`getAverageColor`, `getDominantColor`, and `getHistogram` return JSON strings.
`tile` returns a tiled image. `toBase64` and `toDataUrl` encode an image, and
`fromBase64` decodes an image into a new handle.
`getMode` reports `L`, `LA`, `RGB`, or `RGBA`; grayscale conversion preserves
alpha and therefore returns `LA` when its input has transparency. `getFormat`
and `info` report the detected source format for opened/decoded images and an
empty format string for newly created or transformed images.

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
