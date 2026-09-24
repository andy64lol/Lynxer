//! Clynxer `image` stdlib backend using the Rust `image` crate.
//!
//! Images are kept in a process-local handle table. Structured results cross
//! the native ABI as JSON strings, matching the rest of Clynxer's stdlib ABI.

use base64::{engine::general_purpose::STANDARD, Engine};
use clynxer_abi::{export_int, export_string, lynxer_module};
use image::codecs::jpeg::JpegEncoder;
use image::imageops::{self, FilterType};
use image::{DynamicImage, GenericImage, GenericImageView, ImageFormat, ImageReader, Rgba};
use std::fs::File;
use std::io::Cursor;
use std::sync::{Mutex, OnceLock};

struct Entry {
    image: DynamicImage,
    format: Option<ImageFormat>,
}

static IMAGES: OnceLock<Mutex<Vec<Option<Entry>>>> = OnceLock::new();

fn images() -> &'static Mutex<Vec<Option<Entry>>> {
    IMAGES.get_or_init(|| Mutex::new(Vec::new()))
}

fn store(image: DynamicImage, format: Option<ImageFormat>) -> i64 {
    let mut table = images()
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner());
    table.push(Some(Entry { image, format }));
    (table.len() - 1) as i64
}

fn get(index: i64) -> Option<DynamicImage> {
    if index < 0 {
        return None;
    }
    let table = images().lock().ok()?;
    table
        .get(index as usize)?
        .as_ref()
        .map(|entry| entry.image.clone())
}

fn get_format(index: i64) -> Option<ImageFormat> {
    if index < 0 {
        return None;
    }
    let table = images().lock().ok()?;
    table.get(index as usize)?.as_ref()?.format
}

fn update(index: i64, image: DynamicImage, format: Option<ImageFormat>) -> bool {
    if index < 0 {
        return false;
    }
    let mut table = match images().lock() {
        Ok(table) => table,
        Err(poisoned) => poisoned.into_inner(),
    };
    match table.get_mut(index as usize).and_then(Option::as_mut) {
        Some(entry) => {
            entry.image = image;
            if format.is_some() {
                entry.format = format;
            }
            true
        }
        None => false,
    }
}

fn mode(image: &DynamicImage) -> &'static str {
    match image.color() {
        image::ColorType::L8 | image::ColorType::L16 => "L",
        image::ColorType::La8 | image::ColorType::La16 => "LA",
        image::ColorType::Rgb8 | image::ColorType::Rgb16 | image::ColorType::Rgb32F => "RGB",
        image::ColorType::Rgba8 | image::ColorType::Rgba16 | image::ColorType::Rgba32F => "RGBA",
        _ => "RGB",
    }
}

fn format_name(format: Option<ImageFormat>) -> String {
    match format {
        Some(ImageFormat::Png) => "PNG",
        Some(ImageFormat::Jpeg) => "JPEG",
        Some(ImageFormat::Gif) => "GIF",
        Some(ImageFormat::Bmp) => "BMP",
        Some(ImageFormat::Ico) => "ICO",
        Some(ImageFormat::Tiff) => "TIFF",
        Some(ImageFormat::WebP) => "WEBP",
        _ => "",
    }
    .to_string()
}

fn path_format(path: &str) -> Option<ImageFormat> {
    ImageFormat::from_path(path).ok()
}

fn color(args: &clynxer_abi::Args<'_>, offset: usize, alpha: u8) -> Rgba<u8> {
    Rgba([
        args.int(offset).clamp(0, 255) as u8,
        args.int(offset + 1).clamp(0, 255) as u8,
        args.int(offset + 2).clamp(0, 255) as u8,
        alpha,
    ])
}

fn json_pixel(pixel: Rgba<u8>) -> String {
    format!("[{},{},{},{}]", pixel[0], pixel[1], pixel[2], pixel[3])
}

fn encode_image(image: &DynamicImage, format: ImageFormat, quality: Option<u8>) -> Vec<u8> {
    let mut bytes = Vec::new();
    let mut cursor = Cursor::new(&mut bytes);
    if format == ImageFormat::Jpeg {
        let rgb = image.to_rgb8();
        let mut encoder = JpegEncoder::new_with_quality(&mut cursor, quality.unwrap_or(90));
        let _ = encoder.encode_image(&rgb);
    } else {
        let _ = image.write_to(&mut cursor, format);
    }
    bytes
}

fn format_from_text(text: &str) -> Option<ImageFormat> {
    match text.to_ascii_lowercase().as_str() {
        "png" => Some(ImageFormat::Png),
        "jpg" | "jpeg" => Some(ImageFormat::Jpeg),
        "gif" => Some(ImageFormat::Gif),
        "bmp" => Some(ImageFormat::Bmp),
        "ico" => Some(ImageFormat::Ico),
        "tif" | "tiff" => Some(ImageFormat::Tiff),
        "webp" => Some(ImageFormat::WebP),
        _ => None,
    }
}

fn from_mode(width: u32, height: u32, mode: &str, colour: Rgba<u8>) -> Option<DynamicImage> {
    Some(match mode.to_ascii_uppercase().as_str() {
        "L" | "1" => DynamicImage::ImageLuma8(image::ImageBuffer::from_pixel(
            width,
            height,
            image::Luma([colour[0]]),
        )),
        "RGBA" => DynamicImage::ImageRgba8(image::ImageBuffer::from_pixel(width, height, colour)),
        "RGB" => DynamicImage::ImageRgb8(image::ImageBuffer::from_pixel(
            width,
            height,
            image::Rgb([colour[0], colour[1], colour[2]]),
        )),
        _ => return None,
    })
}

fn rotate(image: DynamicImage, angle: f64) -> DynamicImage {
    let turns = ((angle / 90.0).round() as i32).rem_euclid(4);
    match turns {
        1 => image.rotate90(),
        2 => image.rotate180(),
        3 => image.rotate270(),
        _ => image,
    }
}

fn set_channels(image: &mut DynamicImage, factor: f64, alpha: bool) {
    let mut rgba = image.to_rgba8();
    for pixel in rgba.pixels_mut() {
        for channel in 0..3 {
            pixel[channel] = ((f64::from(pixel[channel]) * factor).round()).clamp(0.0, 255.0) as u8;
        }
        if !alpha {
            pixel[3] = 255;
        }
    }
    *image = DynamicImage::ImageRgba8(rgba);
}

export_int!(image_open, args, {
    let path = args.string(0);
    match ImageReader::open(path) {
        Ok(reader) => {
            let format = reader.format();
            match reader.decode() {
                Ok(image) => store(image, format),
                Err(_) => -1,
            }
        }
        Err(_) => -1,
    }
});

export_int!(image_create, args, {
    let width = args.int(0);
    let height = args.int(1);
    if width <= 0 || height <= 0 {
        -1
    } else {
        from_mode(
            width as u32,
            height as u32,
            args.string(0),
            color(&args, 2, 255),
        )
        .map(|image| store(image, None))
        .unwrap_or(-1)
    }
});

export_int!(image_save, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let path = args.string(0);
    if image.save(path).is_ok() {
        0
    } else {
        -1
    }
});

export_int!(image_save_quality, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let path = args.string(0);
    let quality = args.int(1).clamp(1, 95) as u8;
    if matches!(path_format(path), Some(ImageFormat::Jpeg)) {
        match File::create(path) {
            Ok(mut file) => {
                let rgb = image.to_rgb8();
                let mut encoder = JpegEncoder::new_with_quality(&mut file, quality);
                if encoder.encode_image(&rgb).is_ok() {
                    0
                } else {
                    -1
                }
            }
            Err(_) => -1,
        }
    } else if image.save(path).is_ok() {
        0
    } else {
        -1
    }
});

export_int!(image_copy, args, {
    get(args.int(0))
        .map(|image| store(image, get_format(args.int(0))))
        .unwrap_or(-1)
});

export_int!(image_close, args, {
    if args.int(0) < 0 {
        -1
    } else {
        let mut table = images()
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if let Some(slot) = table.get_mut(args.int(0) as usize) {
            *slot = None;
            0
        } else {
            -1
        }
    }
});

export_int!(image_width, args, {
    get(args.int(0))
        .map(|image| image.width() as i64)
        .unwrap_or(0)
});
export_int!(image_height, args, {
    get(args.int(0))
        .map(|image| image.height() as i64)
        .unwrap_or(0)
});

export_string!(image_mode, args, {
    get(args.int(0))
        .map(|image| mode(&image).to_string())
        .unwrap_or_default()
});
export_string!(image_format, args, { format_name(get_format(args.int(0))) });

export_string!(image_info, args, {
    get(args.int(0))
        .map(|image| {
            format!(
                r#"{{"width":{},"height":{},"mode":"{}","format":"{}"}}"#,
                image.width(),
                image.height(),
                mode(&image),
                format_name(get_format(args.int(0)))
            )
        })
        .unwrap_or_else(|| "{}".to_string())
});

export_int!(image_resize, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let width = args.int(1);
    let height = args.int(2);
    if width <= 0 || height <= 0 {
        -1
    } else {
        store(
            image.resize_exact(width as u32, height as u32, FilterType::Lanczos3),
            None,
        )
    }
});

export_int!(image_thumbnail, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let width = args.int(1);
    let height = args.int(2);
    if width <= 0 || height <= 0 {
        -1
    } else {
        store(image.thumbnail(width as u32, height as u32), None)
    }
});

export_int!(image_scale, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let width = (f64::from(image.width()) * args.num(1)).round().max(1.0) as u32;
    let height = (f64::from(image.height()) * args.num(1)).round().max(1.0) as u32;
    store(
        image.resize_exact(width, height, FilterType::Lanczos3),
        None,
    )
});

export_int!(image_crop, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let x1 = args.int(1).max(0) as u32;
    let y1 = args.int(2).max(0) as u32;
    let x2 = args.int(3).max(args.int(1)) as u32;
    let y2 = args.int(4).max(args.int(2)) as u32;
    if x1 >= image.width() || y1 >= image.height() || x2 <= x1 || y2 <= y1 {
        -1
    } else {
        let width = x2.min(image.width()) - x1;
        let height = y2.min(image.height()) - y1;
        store(image.crop_imm(x1, y1, width, height), None)
    }
});

export_int!(image_rotate, args, {
    get(args.int(0))
        .map(|image| store(rotate(image, args.num(1)), None))
        .unwrap_or(-1)
});
export_int!(image_flip_h, args, {
    get(args.int(0))
        .map(|image| store(image.fliph(), None))
        .unwrap_or(-1)
});
export_int!(image_flip_v, args, {
    get(args.int(0))
        .map(|image| store(image.flipv(), None))
        .unwrap_or(-1)
});

export_int!(image_pad, args, {
    let Some(source) = get(args.int(0)) else {
        return -1;
    };
    let top = args.int(1).max(0) as u32;
    let right = args.int(2).max(0) as u32;
    let bottom = args.int(3).max(0) as u32;
    let left = args.int(4).max(0) as u32;
    let width = source.width().saturating_add(left).saturating_add(right);
    let height = source.height().saturating_add(top).saturating_add(bottom);
    let mut result = DynamicImage::ImageRgba8(image::ImageBuffer::from_pixel(
        width,
        height,
        color(&args, 5, 255),
    ));
    imageops::overlay(&mut result, &source, i64::from(left), i64::from(top));
    store(result, None)
});

export_int!(image_grayscale, args, {
    get(args.int(0))
        .map(|image| store(image.grayscale(), None))
        .unwrap_or(-1)
});
export_int!(image_to_rgb, args, {
    get(args.int(0))
        .map(|image| store(DynamicImage::ImageRgb8(image.to_rgb8()), None))
        .unwrap_or(-1)
});
export_int!(image_to_rgba, args, {
    get(args.int(0))
        .map(|image| store(DynamicImage::ImageRgba8(image.to_rgba8()), None))
        .unwrap_or(-1)
});
export_int!(image_to_binary, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let threshold = args.int(1).clamp(0, 255) as u8;
    let mut result = image.to_luma8();
    for pixel in result.pixels_mut() {
        pixel[0] = if pixel[0] >= threshold { 255 } else { 0 };
    }
    store(DynamicImage::ImageLuma8(result), None)
});

export_int!(image_convert, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    match args.string(0).to_ascii_uppercase().as_str() {
        "L" => store(image.grayscale(), None),
        "RGB" => store(DynamicImage::ImageRgb8(image.to_rgb8()), None),
        "RGBA" => store(DynamicImage::ImageRgba8(image.to_rgba8()), None),
        _ => -1,
    }
});

export_int!(image_brightness, args, {
    let Some(mut image) = get(args.int(0)) else {
        return -1;
    };
    set_channels(&mut image, args.num(1), true);
    store(image, None)
});

export_int!(image_contrast, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    store(
        image.adjust_contrast(((args.num(1) - 1.0) * 100.0) as f32),
        None,
    )
});

export_int!(image_invert, args, {
    let Some(mut image) = get(args.int(0)) else {
        return -1;
    };
    image.invert();
    store(image, None)
});

export_int!(image_blur, args, {
    get(args.int(0))
        .map(|image| store(image.blur(args.num(1) as f32), None))
        .unwrap_or(-1)
});
export_int!(image_box_blur, args, {
    get(args.int(0))
        .map(|image| store(image.blur(args.num(1) as f32), None))
        .unwrap_or(-1)
});
export_int!(image_unsharp, args, {
    get(args.int(0))
        .map(|image| {
            store(
                image.unsharpen(args.num(1) as f32, args.int(3) as i32),
                None,
            )
        })
        .unwrap_or(-1)
});
export_int!(image_sharpen, args, {
    get(args.int(0))
        .map(|image| store(image.unsharpen(1.0, 1), None))
        .unwrap_or(-1)
});

export_string!(image_get_pixel, args, {
    let Some(image) = get(args.int(0)) else {
        return "[]".to_string();
    };
    let x = args.int(1);
    let y = args.int(2);
    if x < 0 || y < 0 || x >= image.width() as i64 || y >= image.height() as i64 {
        "[]".to_string()
    } else {
        json_pixel(image.get_pixel(x as u32, y as u32))
    }
});

export_int!(image_set_pixel, args, {
    let Some(mut image) = get(args.int(0)) else {
        return -1;
    };
    let x = args.int(1);
    let y = args.int(2);
    if x < 0 || y < 0 || x >= image.width() as i64 || y >= image.height() as i64 {
        -1
    } else {
        image.put_pixel(x as u32, y as u32, color(&args, 3, 255));
        if update(args.int(0), image, None) {
            0
        } else {
            -1
        }
    }
});

export_int!(image_set_pixel_a, args, {
    let Some(mut image) = get(args.int(0)) else {
        return -1;
    };
    let x = args.int(1);
    let y = args.int(2);
    if x < 0 || y < 0 || x >= image.width() as i64 || y >= image.height() as i64 {
        -1
    } else {
        image.put_pixel(
            x as u32,
            y as u32,
            color(&args, 3, args.int(6).clamp(0, 255) as u8),
        );
        if update(args.int(0), image, None) {
            0
        } else {
            -1
        }
    }
});

export_int!(image_fill, args, {
    let Some(mut image) = get(args.int(0)) else {
        return -1;
    };
    for y in 0..image.height() {
        for x in 0..image.width() {
            image.put_pixel(x, y, color(&args, 1, 255));
        }
    }
    if update(args.int(0), image, None) {
        0
    } else {
        -1
    }
});

export_int!(image_paste, args, {
    let Some(mut destination) = get(args.int(0)) else {
        return -1;
    };
    let Some(source) = get(args.int(1)) else {
        return -1;
    };
    imageops::replace(&mut destination, &source, args.int(2), args.int(3));
    if update(args.int(0), destination, None) {
        0
    } else {
        -1
    }
});

export_int!(image_paste_alpha, args, {
    let Some(mut destination) = get(args.int(0)) else {
        return -1;
    };
    let Some(source) = get(args.int(1)) else {
        return -1;
    };
    imageops::overlay(&mut destination, &source, args.int(2), args.int(3));
    if update(args.int(0), destination, None) {
        0
    } else {
        -1
    }
});

export_int!(image_blend, args, {
    let Some(first) = get(args.int(0)) else {
        return -1;
    };
    let Some(second) = get(args.int(1)) else {
        return -1;
    };
    let alpha = args.num(2).clamp(0.0, 1.0);
    let width = first.width().min(second.width());
    let height = first.height().min(second.height());
    let mut result = first.to_rgba8();
    let other = second.to_rgba8();
    for y in 0..height {
        for x in 0..width {
            let a = result.get_pixel_mut(x, y);
            let b = other.get_pixel(x, y);
            for channel in 0..4 {
                a[channel] = (f64::from(a[channel]) * (1.0 - alpha) + f64::from(b[channel]) * alpha)
                    .round() as u8;
            }
        }
    }
    store(DynamicImage::ImageRgba8(result), None)
});

export_int!(image_add_alpha, args, {
    get(args.int(0))
        .map(|image| store(DynamicImage::ImageRgba8(image.to_rgba8()), None))
        .unwrap_or(-1)
});
export_int!(image_set_alpha, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let mut result = image.to_rgba8();
    let alpha = args.int(1).clamp(0, 255) as u8;
    for pixel in result.pixels_mut() {
        pixel[3] = alpha;
    }
    store(DynamicImage::ImageRgba8(result), None)
});
export_int!(image_remove_alpha, args, {
    get(args.int(0))
        .map(|image| store(DynamicImage::ImageRgb8(image.to_rgb8()), None))
        .unwrap_or(-1)
});

export_string!(image_average_color, args, {
    let Some(image) = get(args.int(0)) else {
        return "[]".to_string();
    };
    let rgba = image.to_rgba8();
    let count = f64::from(rgba.width()) * f64::from(rgba.height());
    if count == 0.0 {
        return "[]".to_string();
    }
    let mut sums = [0.0; 4];
    for pixel in rgba.pixels() {
        for channel in 0..4 {
            sums[channel] += f64::from(pixel[channel]);
        }
    }
    format!(
        "[{:.0},{:.0},{:.0},{:.0}]",
        sums[0] / count,
        sums[1] / count,
        sums[2] / count,
        sums[3] / count
    )
});

export_string!(image_dominant_color, args, {
    let Some(image) = get(args.int(0)) else {
        return "[]".to_string();
    };
    let mut buckets = std::collections::HashMap::<[u8; 4], usize>::new();
    for pixel in image.to_rgba8().pixels() {
        let bucket = [
            pixel[0] / 16 * 16,
            pixel[1] / 16 * 16,
            pixel[2] / 16 * 16,
            pixel[3],
        ];
        *buckets.entry(bucket).or_default() += 1;
    }
    buckets
        .into_iter()
        .max_by_key(|(_, count)| *count)
        .map(|(pixel, _)| json_pixel(Rgba(pixel)))
        .unwrap_or_else(|| "[]".to_string())
});

export_string!(image_histogram, args, {
    let Some(image) = get(args.int(0)) else {
        return "{}".to_string();
    };
    let mut channels = [[0usize; 256]; 4];
    for pixel in image.to_rgba8().pixels() {
        for channel in 0..4 {
            channels[channel][pixel[channel] as usize] += 1;
        }
    }
    let channel_json = |values: &[usize; 256]| -> String {
        format!(
            "[{}]",
            values
                .iter()
                .map(|value| value.to_string())
                .collect::<Vec<_>>()
                .join(",")
        )
    };
    format!(
        r#"{{"r":{},"g":{},"b":{},"a":{}}}"#,
        channel_json(&channels[0]),
        channel_json(&channels[1]),
        channel_json(&channels[2]),
        channel_json(&channels[3])
    )
});

export_int!(image_tile, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let cols = args.int(1);
    let rows = args.int(2);
    if cols <= 0 || rows <= 0 {
        return -1;
    }
    let mut result = DynamicImage::ImageRgba8(image::ImageBuffer::new(
        image.width() * cols as u32,
        image.height() * rows as u32,
    ));
    for row in 0..rows {
        for col in 0..cols {
            imageops::overlay(
                &mut result,
                &image,
                i64::from(col) * i64::from(image.width()),
                i64::from(row) * i64::from(image.height()),
            );
        }
    }
    store(result, None)
});

export_string!(image_base64, args, {
    let Some(image) = get(args.int(0)) else {
        return String::new();
    };
    let format = format_from_text(args.string(0))
        .or_else(|| get_format(args.int(0)))
        .unwrap_or(ImageFormat::Png);
    STANDARD.encode(encode_image(&image, format, None))
});

export_int!(image_from_base64, args, {
    let bytes = match STANDARD.decode(args.string(0)) {
        Ok(bytes) => bytes,
        Err(_) => return -1,
    };
    // The bytes carry their format, so record it rather than storing `None`:
    // `getFormat` and `info` report it, and the reference does too.
    let format = image::guess_format(&bytes).ok();
    match image::load_from_memory(&bytes) {
        Ok(image) => store(image, format),
        Err(_) => -1,
    }
});

export_string!(image_data_url, args, {
    let Some(image) = get(args.int(0)) else {
        return String::new();
    };
    let format = format_from_text(args.string(0)).unwrap_or(ImageFormat::Png);
    let mime = match format {
        ImageFormat::Jpeg => "image/jpeg",
        ImageFormat::WebP => "image/webp",
        ImageFormat::Gif => "image/gif",
        _ => "image/png",
    };
    format!(
        "data:{mime};base64,{}",
        STANDARD.encode(encode_image(&image, format, None))
    )
});

const OPS: &[(&str, &str, &str)] = &[
    ("open", "image_open", "cdecl:int64(...)"),
    ("create", "image_create", "cdecl:int64(...)"),
    ("save", "image_save", "cdecl:int64(...)"),
    ("saveQuality", "image_save_quality", "cdecl:int64(...)"),
    ("copy", "image_copy", "cdecl:int64(...)"),
    ("close", "image_close", "cdecl:int64(...)"),
    ("getWidth", "image_width", "cdecl:int64(...)"),
    ("getHeight", "image_height", "cdecl:int64(...)"),
    ("getMode", "image_mode", "cdecl:cstring(...)"),
    ("getFormat", "image_format", "cdecl:cstring(...)"),
    ("info", "image_info", "cdecl:cstring(...)"),
    ("resize", "image_resize", "cdecl:int64(...)"),
    ("thumbnail", "image_thumbnail", "cdecl:int64(...)"),
    ("scale", "image_scale", "cdecl:int64(...)"),
    ("crop", "image_crop", "cdecl:int64(...)"),
    ("rotate", "image_rotate", "cdecl:int64(...)"),
    ("flipH", "image_flip_h", "cdecl:int64(...)"),
    ("flipV", "image_flip_v", "cdecl:int64(...)"),
    ("pad", "image_pad", "cdecl:int64(...)"),
    ("convert", "image_convert", "cdecl:int64(...)"),
    ("grayscale", "image_grayscale", "cdecl:int64(...)"),
    ("toRGB", "image_to_rgb", "cdecl:int64(...)"),
    ("toRGBA", "image_to_rgba", "cdecl:int64(...)"),
    ("toBinary", "image_to_binary", "cdecl:int64(...)"),
    ("brightness", "image_brightness", "cdecl:int64(...)"),
    ("contrast", "image_contrast", "cdecl:int64(...)"),
    ("invert", "image_invert", "cdecl:int64(...)"),
    ("blur", "image_blur", "cdecl:int64(...)"),
    ("boxBlur", "image_box_blur", "cdecl:int64(...)"),
    ("unsharpMask", "image_unsharp", "cdecl:int64(...)"),
    ("sharpen", "image_sharpen", "cdecl:int64(...)"),
    ("getPixel", "image_get_pixel", "cdecl:cstring(...)"),
    ("setPixel", "image_set_pixel", "cdecl:int64(...)"),
    ("setPixelA", "image_set_pixel_a", "cdecl:int64(...)"),
    ("fill", "image_fill", "cdecl:int64(...)"),
    ("paste", "image_paste", "cdecl:int64(...)"),
    ("pasteWithAlpha", "image_paste_alpha", "cdecl:int64(...)"),
    ("blend", "image_blend", "cdecl:int64(...)"),
    ("addAlpha", "image_add_alpha", "cdecl:int64(...)"),
    ("setAlpha", "image_set_alpha", "cdecl:int64(...)"),
    ("removeAlpha", "image_remove_alpha", "cdecl:int64(...)"),
    (
        "getAverageColor",
        "image_average_color",
        "cdecl:cstring(...)",
    ),
    (
        "getDominantColor",
        "image_dominant_color",
        "cdecl:cstring(...)",
    ),
    ("getHistogram", "image_histogram", "cdecl:cstring(...)"),
    ("tile", "image_tile", "cdecl:int64(...)"),
    ("toBase64", "image_base64", "cdecl:cstring(...)"),
    ("fromBase64", "image_from_base64", "cdecl:int64(...)"),
    ("toDataUrl", "image_data_url", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);
