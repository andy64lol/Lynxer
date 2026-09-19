# image

Image loading, creation, transforms, pixels, and encoding. The backend is a
Rust `cdylib` built with the `image` crate and installed as
`stdlib/image.so`. Images are process-local integer handles; `-1` indicates a
failed operation.

> **Requires:** a Rust toolchain (`cargo`). Build it with `make rust` or
> `make all`. The module supports PNG, JPEG, GIF, BMP, TIFF, and WebP.

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
| `open(path)` | Decode an image file and return a handle. |
| `create(width, height, mode, r, g, b)` | Create an image in `L`, `RGB`, or `RGBA` mode. |
| `copy(handle)` | Clone an image into a new handle. |
| `close(handle)` | Release a handle. |
| `getWidth` / `getHeight` | Read dimensions. |
| `getMode` / `getFormat` | Read the color mode and detected source format. |
| `info(handle)` | Return JSON metadata containing width, height, mode, and format. |
| `save(handle, path)` | Encode using the path extension. |
| `saveQuality(handle, path, quality)` | Save JPEG with quality from 1 to 95. |

## Transforms and pixels

`resize`, `thumbnail`, `scale`, `crop`, `rotate`, `flipH`, `flipV`, `pad`,
`convert`, `grayscale`, `toRGB`, `toRGBA`, `toBinary`, `brightness`, `contrast`,
`invert`, `blur`, `boxBlur`, `unsharpMask`, and `sharpen` return new handles.

`getPixel` returns a JSON array `[r,g,b,a]`. `setPixel`, `setPixelA`, and `fill`
mutate the existing image. `paste` and `pasteWithAlpha` mutate the destination;
`blend` returns a new image. `addAlpha`, `setAlpha`, and `removeAlpha` return
new handles.

## Analysis and encoding

`getAverageColor`, `getDominantColor`, and `getHistogram` return JSON strings.
`tile` returns a tiled image. `toBase64` and `toDataUrl` encode an image, and
`fromBase64` decodes an image into a new handle.
