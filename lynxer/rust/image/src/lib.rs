//! Lynxer `image` stdlib backend using the Rust `image` crate.
//!
//! Images are kept in a process-local handle table. Structured results cross
//! the native ABI as JSON strings, matching the rest of Lynxer's stdlib ABI.

use base64::{engine::general_purpose::STANDARD, Engine};
use lynxer_abi::{export_int, export_string, lynxer_module};
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

fn color(args: &lynxer_abi::Args<'_>, offset: usize, alpha: u8) -> Rgba<u8> {
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

// --- Extended: geometry, filters, compositing, analysis and drawing ---------

fn clamp_byte(value: f64) -> u8 {
    value.round().clamp(0.0, 255.0) as u8
}

fn rgb(args: &lynxer_abi::Args<'_>, offset: usize) -> Rgba<u8> {
    Rgba([
        clamp_byte(args.num(offset)),
        clamp_byte(args.num(offset + 1)),
        clamp_byte(args.num(offset + 2)),
        255,
    ])
}

/// A mean-preserving 3x3 convolution: the divisor is applied to the kernel
/// before `filter3x3`, which clamps each channel to `0..=255`. The convolution
/// runs on the colour channels only — filtering alpha through a kernel that sums
/// to anything but 1 would destroy it — and the original alpha is restored.
fn convolve3x3(image: &DynamicImage, divisor: f64, kernel: [f64; 9]) -> DynamicImage {
    let scaled: [f32; 9] = std::array::from_fn(|index| (kernel[index] / divisor) as f32);
    let mut output = image.to_rgba8();
    let filtered = imageops::filter3x3(&image.to_rgb8(), &scaled);
    for (target, value) in output.pixels_mut().zip(filtered.pixels()) {
        target[0] = value[0];
        target[1] = value[1];
        target[2] = value[2];
    }
    DynamicImage::ImageRgba8(output)
}

/// A 3x3 convolution with a constant offset — what an emboss filter needs to
/// keep its result around mid-grey instead of clipping to black.
fn convolve3x3_offset(image: &DynamicImage, kernel: [f64; 9], offset: f64) -> DynamicImage {
    let rgba = image.to_rgba8();
    let (width, height) = rgba.dimensions();
    let mut output = rgba.clone();
    for y in 0..height as i64 {
        for x in 0..width as i64 {
            for channel in 0..3 {
                let mut sum = 0.0;
                for dy in 0..3i64 {
                    for dx in 0..3i64 {
                        let sx = (x + dx - 1).clamp(0, width as i64 - 1) as u32;
                        let sy = (y + dy - 1).clamp(0, height as i64 - 1) as u32;
                        sum += f64::from(rgba.get_pixel(sx, sy)[channel])
                            * kernel[(dy * 3 + dx) as usize];
                    }
                }
                output.get_pixel_mut(x as u32, y as u32)[channel] = clamp_byte(sum + offset);
            }
        }
    }
    DynamicImage::ImageRgba8(output)
}

fn pick_median(values: &mut Vec<u8>) -> u8 {
    values.sort_unstable();
    values[values.len() / 2]
}

fn pick_min(values: &mut Vec<u8>) -> u8 {
    values.iter().copied().min().unwrap_or(0)
}

fn pick_max(values: &mut Vec<u8>) -> u8 {
    values.iter().copied().max().unwrap_or(0)
}

/// A rank filter over a square window. `size` is forced odd, and edges clamp to
/// the border pixel, matching the fixed-window filters elsewhere.
fn rank_filter(image: &DynamicImage, size: i64, pick: fn(&mut Vec<u8>) -> u8) -> DynamicImage {
    let radius = (size.max(1) | 1) / 2;
    let rgba = image.to_rgba8();
    let (width, height) = rgba.dimensions();
    let mut output = rgba.clone();
    let mut window: Vec<u8> = Vec::new();
    for y in 0..height as i64 {
        for x in 0..width as i64 {
            for channel in 0..4 {
                window.clear();
                for dy in -radius..=radius {
                    for dx in -radius..=radius {
                        let sx = (x + dx).clamp(0, width as i64 - 1) as u32;
                        let sy = (y + dy).clamp(0, height as i64 - 1) as u32;
                        window.push(rgba.get_pixel(sx, sy)[channel]);
                    }
                }
                output.get_pixel_mut(x as u32, y as u32)[channel] = pick(&mut window);
            }
        }
    }
    DynamicImage::ImageRgba8(output)
}

fn fit_to(image: DynamicImage, width: u32, height: u32) -> DynamicImage {
    let (source_width, source_height) = (image.width(), image.height());
    if width == 0 || height == 0 || source_width == 0 || source_height == 0 {
        return image;
    }
    let scale = f64::max(
        f64::from(width) / f64::from(source_width),
        f64::from(height) / f64::from(source_height),
    );
    let scaled_width = ((f64::from(source_width) * scale).round() as u32).max(width);
    let scaled_height = ((f64::from(source_height) * scale).round() as u32).max(height);
    let resized = image.resize_exact(scaled_width, scaled_height, FilterType::Lanczos3);
    resized.crop_imm(
        (scaled_width - width) / 2,
        (scaled_height - height) / 2,
        width,
        height,
    )
}

fn contain_in(image: DynamicImage, width: u32, height: u32, background: Rgba<u8>) -> DynamicImage {
    let (source_width, source_height) = (image.width(), image.height());
    if width == 0 || height == 0 || source_width == 0 || source_height == 0 {
        return image;
    }
    let scale = f64::min(
        f64::from(width) / f64::from(source_width),
        f64::from(height) / f64::from(source_height),
    );
    let scaled_width = ((f64::from(source_width) * scale).round() as u32).clamp(1, width);
    let scaled_height = ((f64::from(source_height) * scale).round() as u32).clamp(1, height);
    let resized = image.resize_exact(scaled_width, scaled_height, FilterType::Lanczos3);
    let mut canvas = image::RgbaImage::from_pixel(width, height, background);
    imageops::overlay(
        &mut canvas,
        &resized.to_rgba8(),
        i64::from((width - scaled_width) / 2),
        i64::from((height - scaled_height) / 2),
    );
    DynamicImage::ImageRgba8(canvas)
}

/// Pillow's `ImageEnhance.Color`: blend between the grey version and the
/// original, so `0.0` is greyscale and `1.0` is unchanged.
fn enhance_color(image: &DynamicImage, factor: f64) -> DynamicImage {
    let mut rgba = image.to_rgba8();
    for pixel in rgba.pixels_mut() {
        let grey = 0.299 * f64::from(pixel[0])
            + 0.587 * f64::from(pixel[1])
            + 0.114 * f64::from(pixel[2]);
        for channel in 0..3 {
            pixel[channel] = clamp_byte(grey + factor * (f64::from(pixel[channel]) - grey));
        }
    }
    DynamicImage::ImageRgba8(rgba)
}

/// Pillow's `ImageEnhance.Sharpness`: blend between a blurred copy and the
/// original, so `0.0` is blur and `2.0` extrapolates beyond the original.
fn enhance_sharpness(image: &DynamicImage, factor: f64) -> DynamicImage {
    // `imageops::blur` keeps the pixel type, so the blurred copy is already an
    // RGBA buffer.
    let blurred = imageops::blur(image, 1.0);
    let mut result = image.to_rgba8();
    for (pixel, blur) in result.pixels_mut().zip(blurred.pixels()) {
        for channel in 0..4 {
            pixel[channel] = clamp_byte(
                f64::from(blur[channel]) * (1.0 - factor)
                    + f64::from(pixel[channel]) * factor,
            );
        }
    }
    DynamicImage::ImageRgba8(result)
}

fn composite_over(base: &DynamicImage, overlay: &DynamicImage, mask: &DynamicImage) -> DynamicImage {
    let mut result = base.to_rgba8();
    let overlay = overlay.to_rgba8();
    let mask = mask.to_rgba8();
    let (width, height) = result.dimensions();
    for y in 0..height {
        for x in 0..width {
            let coverage = if x < mask.width() && y < mask.height() {
                f64::from(mask.get_pixel(x, y)[0]) / 255.0
            } else {
                0.0
            };
            let source = if x < overlay.width() && y < overlay.height() {
                overlay.get_pixel(x, y).0
            } else {
                [0, 0, 0, 0]
            };
            let pixel = result.get_pixel_mut(x, y);
            for channel in 0..4 {
                pixel[channel] = clamp_byte(
                    f64::from(pixel[channel]) * (1.0 - coverage)
                        + f64::from(source[channel]) * coverage,
                );
            }
        }
    }
    DynamicImage::ImageRgba8(result)
}

/// Stretches each colour channel's `[min, max]` range across the full byte
/// range. The alpha channel is left alone.
fn auto_contrast(image: &DynamicImage) -> DynamicImage {
    let mut rgba = image.to_rgba8();
    for channel in 0..3 {
        let mut low = u8::MAX;
        let mut high = u8::MIN;
        for pixel in rgba.pixels() {
            low = low.min(pixel[channel]);
            high = high.max(pixel[channel]);
        }
        if high <= low {
            continue;
        }
        let span = f64::from(high - low);
        for pixel in rgba.pixels_mut() {
            pixel[channel] =
                clamp_byte((f64::from(pixel[channel]) - f64::from(low)) * 255.0 / span);
        }
    }
    DynamicImage::ImageRgba8(rgba)
}

/// Histogram equalisation: map each channel through its cumulative distribution,
/// with the lowest populated level subtracted so a flat image is unchanged.
fn equalize(image: &DynamicImage) -> DynamicImage {
    let mut rgba = image.to_rgba8();
    for channel in 0..3 {
        let mut histogram = [0usize; 256];
        for pixel in rgba.pixels() {
            histogram[pixel[channel] as usize] += 1;
        }
        let total: usize = histogram.iter().sum();
        if total == 0 {
            continue;
        }
        let mut cumulative = [0usize; 256];
        let mut running = 0usize;
        for level in 0..256 {
            running += histogram[level];
            cumulative[level] = running;
        }
        let lowest = cumulative.iter().copied().find(|count| *count > 0).unwrap_or(0);
        let denominator = total.saturating_sub(lowest);
        if denominator == 0 {
            continue;
        }
        let mut lookup = [0u8; 256];
        for level in 0..256 {
            let shifted = cumulative[level].saturating_sub(lowest);
            lookup[level] = clamp_byte(shifted as f64 * 255.0 / denominator as f64);
        }
        for pixel in rgba.pixels_mut() {
            pixel[channel] = lookup[pixel[channel] as usize];
        }
    }
    DynamicImage::ImageRgba8(rgba)
}

/// Inverts every channel value at or above `threshold`.
fn solarize(image: &DynamicImage, threshold: i64) -> DynamicImage {
    let threshold = threshold.clamp(0, 255) as u8;
    let mut rgba = image.to_rgba8();
    for pixel in rgba.pixels_mut() {
        for channel in 0..3 {
            if pixel[channel] >= threshold {
                pixel[channel] = 255 - pixel[channel];
            }
        }
    }
    DynamicImage::ImageRgba8(rgba)
}

/// Keeps the top `bits` bits of every colour channel.
fn posterize(image: &DynamicImage, bits: i64) -> DynamicImage {
    let bits = bits.clamp(1, 8) as u32;
    let mask = if bits >= 8 {
        0xFFu8
    } else {
        (0xFFu8 << (8 - bits)) & 0xFF
    };
    let mut rgba = image.to_rgba8();
    for pixel in rgba.pixels_mut() {
        for channel in 0..3 {
            pixel[channel] &= mask;
        }
    }
    DynamicImage::ImageRgba8(rgba)
}

/// Median-cut colour quantisation, then nearest-colour mapping. Only the colour
/// channels are quantised; alpha is preserved.
fn quantize(image: &DynamicImage, colors: i64) -> DynamicImage {
    let colors = colors.clamp(1, 256) as usize;
    let mut rgba = image.to_rgba8();
    let mut boxes: Vec<Vec<[u8; 3]>> = vec![rgba.pixels().map(|pixel| [pixel[0], pixel[1], pixel[2]]).collect()];
    while boxes.len() < colors {
        let mut target: Option<(usize, usize, u8)> = None;
        for (index, bucket) in boxes.iter().enumerate() {
            if bucket.len() < 2 {
                continue;
            }
            for channel in 0..3 {
                let low = bucket.iter().map(|pixel| pixel[channel]).min().unwrap_or(0);
                let high = bucket.iter().map(|pixel| pixel[channel]).max().unwrap_or(0);
                let range = high - low;
                if target.map(|(_, _, best)| range > best).unwrap_or(true) {
                    target = Some((index, channel, range));
                }
            }
        }
        let Some((index, channel, _)) = target else {
            break;
        };
        let mut bucket = std::mem::take(&mut boxes[index]);
        bucket.sort_unstable_by_key(|pixel| pixel[channel]);
        let right = bucket.split_off(bucket.len() / 2);
        boxes[index] = bucket;
        boxes.push(right);
    }
    let palette: Vec<[u8; 3]> = boxes
        .iter()
        .map(|bucket| {
            let count = bucket.len().max(1) as u32;
            let mut sums = [0u32; 3];
            for pixel in bucket {
                for channel in 0..3 {
                    sums[channel] += u32::from(pixel[channel]);
                }
            }
            [
                (sums[0] / count) as u8,
                (sums[1] / count) as u8,
                (sums[2] / count) as u8,
            ]
        })
        .collect();
    for pixel in rgba.pixels_mut() {
        let mut best = 0usize;
        let mut best_distance = u32::MAX;
        for (index, colour) in palette.iter().enumerate() {
            let distance: u32 = (0..3)
                .map(|channel| {
                    let delta = i32::from(pixel[channel]) - i32::from(colour[channel]);
                    (delta * delta) as u32
                })
                .sum();
            if distance < best_distance {
                best_distance = distance;
                best = index;
            }
        }
        pixel[0] = palette[best][0];
        pixel[1] = palette[best][1];
        pixel[2] = palette[best][2];
    }
    DynamicImage::ImageRgba8(rgba)
}

/// Splits into four `L` band images and returns `"r,g,b,a"` indices.
fn split_channels(image: &DynamicImage) -> String {
    let rgba = image.to_rgba8();
    let (width, height) = rgba.dimensions();
    let mut indices = [-1i64; 4];
    for channel in 0..4 {
        let mut band = image::GrayImage::new(width, height);
        for (x, y, pixel) in rgba.enumerate_pixels() {
            band.put_pixel(x, y, image::Luma([pixel[channel]]));
        }
        indices[channel] = store(DynamicImage::ImageLuma8(band), None);
    }
    format!("{},{},{},{}", indices[0], indices[1], indices[2], indices[3])
}

/// Merges up to four `L` band images into `RGBA`. A missing or invalid alpha
/// image means fully opaque.
fn merge_channels(indices: [i64; 4]) -> Option<DynamicImage> {
    let sources: Vec<Option<image::RgbaImage>> = indices
        .iter()
        .map(|index| get(*index).map(|image| image.to_rgba8()))
        .collect();
    let (width, height) = sources[0].as_ref()?.dimensions();
    let mut output = image::RgbaImage::new(width, height);
    for y in 0..height {
        for x in 0..width {
            let mut value = [0u8; 4];
            for channel in 0..4 {
                value[channel] = match &sources[channel] {
                    Some(source) if x < source.width() && y < source.height() => {
                        source.get_pixel(x, y)[0]
                    }
                    _ if channel == 3 => 255,
                    _ => 0,
                };
            }
            output.put_pixel(x, y, Rgba(value));
        }
    }
    Some(DynamicImage::ImageRgba8(output))
}

// --- Drawing ---------------------------------------------------------------

fn put_pixel(buffer: &mut image::RgbaImage, x: i64, y: i64, colour: Rgba<u8>) {
    if x < 0 || y < 0 || x >= buffer.width() as i64 || y >= buffer.height() as i64 {
        return;
    }
    buffer.put_pixel(x as u32, y as u32, colour);
}

fn draw_line(
    buffer: &mut image::RgbaImage,
    x1: i64,
    y1: i64,
    x2: i64,
    y2: i64,
    colour: Rgba<u8>,
    line_width: i64,
) {
    let half = (line_width.max(1) - 1) / 2;
    let (dx, dy) = ((x2 - x1).abs(), -(y2 - y1).abs());
    let (sx, sy) = (if x1 < x2 { 1 } else { -1 }, if y1 < y2 { 1 } else { -1 });
    let (mut x, mut y) = (x1, y1);
    let mut error = dx + dy;
    loop {
        for oy in -half..=half {
            for ox in -half..=half {
                put_pixel(buffer, x + ox, y + oy, colour);
            }
        }
        if x == x2 && y == y2 {
            break;
        }
        let doubled = 2 * error;
        if doubled >= dy {
            error += dy;
            x += sx;
        }
        if doubled <= dx {
            error += dx;
            y += sy;
        }
    }
}

fn draw_rect(
    buffer: &mut image::RgbaImage,
    x1: i64,
    y1: i64,
    x2: i64,
    y2: i64,
    colour: Rgba<u8>,
    fill: bool,
) {
    let (left, right) = (x1.min(x2), x1.max(x2));
    let (top, bottom) = (y1.min(y2), y1.max(y2));
    if fill {
        for y in top..=bottom {
            for x in left..=right {
                put_pixel(buffer, x, y, colour);
            }
        }
        return;
    }
    for x in left..=right {
        put_pixel(buffer, x, top, colour);
        put_pixel(buffer, x, bottom, colour);
    }
    for y in top..=bottom {
        put_pixel(buffer, left, y, colour);
        put_pixel(buffer, right, y, colour);
    }
}

fn rounded_inset(row: i64, top: i64, bottom: i64, radius: i64) -> i64 {
    let radius = radius.max(0);
    if radius == 0 {
        return 0;
    }
    let offset = if row < top + radius {
        (top + radius) - row
    } else if row > bottom - radius {
        row - (bottom - radius)
    } else {
        return 0;
    };
    let remaining = radius * radius - offset * offset;
    if remaining <= 0 {
        return radius;
    }
    radius - (remaining as f64).sqrt().round() as i64
}

fn draw_rounded_rect(
    buffer: &mut image::RgbaImage,
    x1: i64,
    y1: i64,
    x2: i64,
    y2: i64,
    colour: Rgba<u8>,
    fill: bool,
    radius: i64,
) {
    let (left, right) = (x1.min(x2), x1.max(x2));
    let (top, bottom) = (y1.min(y2), y1.max(y2));
    for y in top..=bottom {
        let inset = rounded_inset(y, top, bottom, radius);
        let (from, to) = (left + inset, right - inset);
        if from > to {
            continue;
        }
        if fill {
            for x in from..=to {
                put_pixel(buffer, x, y, colour);
            }
            continue;
        }
        if y == top || y == bottom {
            for x in from..=to {
                put_pixel(buffer, x, y, colour);
            }
        }
        put_pixel(buffer, from, y, colour);
        put_pixel(buffer, to, y, colour);
    }
}

fn draw_circle(
    buffer: &mut image::RgbaImage,
    cx: i64,
    cy: i64,
    radius: i64,
    colour: Rgba<u8>,
    fill: bool,
) {
    if radius < 0 {
        return;
    }
    if fill {
        for y in (cy - radius)..=(cy + radius) {
            let dy = y - cy;
            let remaining = radius * radius - dy * dy;
            if remaining < 0 {
                continue;
            }
            let half = (remaining as f64).sqrt().round() as i64;
            for x in (cx - half)..=(cx + half) {
                put_pixel(buffer, x, y, colour);
            }
        }
        return;
    }
    let (mut x, mut y) = (radius, 0i64);
    let mut error = 1 - radius;
    while x >= y {
        for (dx, dy) in [
            (x, y),
            (y, x),
            (-x, y),
            (-y, x),
            (x, -y),
            (y, -x),
            (-x, -y),
            (-y, -x),
        ] {
            put_pixel(buffer, cx + dx, cy + dy, colour);
        }
        y += 1;
        if error < 0 {
            error += 2 * y + 1;
        } else {
            x -= 1;
            error += 2 * (y - x) + 1;
        }
    }
}

fn draw_ellipse(
    buffer: &mut image::RgbaImage,
    x1: i64,
    y1: i64,
    x2: i64,
    y2: i64,
    colour: Rgba<u8>,
    fill: bool,
) {
    let (left, right) = (x1.min(x2), x1.max(x2));
    let (top, bottom) = (y1.min(y2), y1.max(y2));
    let cx = (left + right) as f64 / 2.0;
    let cy = (top + bottom) as f64 / 2.0;
    let rx = (right - left) as f64 / 2.0;
    let ry = (bottom - top) as f64 / 2.0;
    if rx <= 0.0 || ry <= 0.0 {
        draw_line(buffer, left, top, right, bottom, colour, 1);
        return;
    }
    for y in top..=bottom {
        let normalised = (y as f64 - cy) / ry;
        let remaining = 1.0 - normalised * normalised;
        if remaining < 0.0 {
            continue;
        }
        let half = rx * remaining.sqrt();
        if fill {
            for x in (cx - half).round() as i64..=(cx + half).round() as i64 {
                put_pixel(buffer, x, y, colour);
            }
        } else {
            put_pixel(buffer, (cx - half).round() as i64, y, colour);
            put_pixel(buffer, (cx + half).round() as i64, y, colour);
        }
    }
    if !fill {
        for x in left..=right {
            let normalised = (x as f64 - cx) / rx;
            let remaining = 1.0 - normalised * normalised;
            if remaining < 0.0 {
                continue;
            }
            let half = ry * remaining.sqrt();
            put_pixel(buffer, x, (cy - half).round() as i64, colour);
            put_pixel(buffer, x, (cy + half).round() as i64, colour);
        }
    }
}

fn draw_polygon(buffer: &mut image::RgbaImage, points: &[(i64, i64)], colour: Rgba<u8>) {
    if points.len() < 3 {
        return;
    }
    let top = points.iter().map(|point| point.1).min().unwrap_or(0);
    let bottom = points.iter().map(|point| point.1).max().unwrap_or(0);
    for y in top..=bottom {
        let scan = y as f64 + 0.5;
        let mut crossings: Vec<f64> = Vec::new();
        for index in 0..points.len() {
            let (x1, y1) = (points[index].0 as f64, points[index].1 as f64);
            let (x2, y2) = (
                points[(index + 1) % points.len()].0 as f64,
                points[(index + 1) % points.len()].1 as f64,
            );
            if (y1 <= scan && y2 > scan) || (y2 <= scan && y1 > scan) {
                crossings.push(x1 + (scan - y1) / (y2 - y1) * (x2 - x1));
            }
        }
        crossings.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
        let mut index = 0;
        while index + 1 < crossings.len() {
            for x in (crossings[index].round() as i64)..=(crossings[index + 1].round() as i64) {
                put_pixel(buffer, x, y, colour);
            }
            index += 2;
        }
    }
}

/// Parses the flat `[x,y,x,y,…]` coordinate array `drawPolygon` takes.
fn parse_points(text: &str) -> Vec<(i64, i64)> {
    let numbers: Vec<f64> = text
        .split(|character: char| {
            character == ',' || character == '[' || character == ']' || character.is_whitespace()
        })
        .filter(|part| !part.is_empty())
        .filter_map(|part| part.parse::<f64>().ok())
        .collect();
    numbers
        .chunks_exact(2)
        .map(|pair| (pair[0].round() as i64, pair[1].round() as i64))
        .collect()
}

fn flood_fill(buffer: &mut image::RgbaImage, x: i64, y: i64, colour: Rgba<u8>) {
    if x < 0 || y < 0 || x >= buffer.width() as i64 || y >= buffer.height() as i64 {
        return;
    }
    let seed = buffer.get_pixel(x as u32, y as u32).0;
    if seed == colour.0 {
        return;
    }
    let mut stack = vec![(x, y)];
    while let Some((cx, cy)) = stack.pop() {
        if cx < 0 || cy < 0 || cx >= buffer.width() as i64 || cy >= buffer.height() as i64 {
            continue;
        }
        if buffer.get_pixel(cx as u32, cy as u32).0 != seed {
            continue;
        }
        buffer.put_pixel(cx as u32, cy as u32, colour);
        stack.push((cx + 1, cy));
        stack.push((cx - 1, cy));
        stack.push((cx, cy + 1));
        stack.push((cx, cy - 1));
    }
}

// --- Text ------------------------------------------------------------------

const SYSTEM_FONTS: &[&str] = &[
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
];

/// The first readable system font: the known paths first, then any TTF/OTF
/// under the usual font root.
fn system_font_bytes() -> Option<Vec<u8>> {
    for path in SYSTEM_FONTS {
        if let Ok(bytes) = std::fs::read(path) {
            return Some(bytes);
        }
    }
    let root = std::path::Path::new("/usr/share/fonts");
    if !root.is_dir() {
        return None;
    }
    let mut pending = vec![root.to_path_buf()];
    while let Some(directory) = pending.pop() {
        let Ok(entries) = std::fs::read_dir(&directory) else {
            continue;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                pending.push(path);
                continue;
            }
            if matches!(path.extension().and_then(|value| value.to_str()), Some("ttf") | Some("otf")) {
                if let Ok(bytes) = std::fs::read(&path) {
                    return Some(bytes);
                }
            }
        }
    }
    None
}

/// Draws `text` with its top-left at `(origin_x, origin_y)`, blending each glyph
/// by its coverage so the background shows through the edges.
fn draw_text_into(
    buffer: &mut image::RgbaImage,
    origin_x: i64,
    origin_y: i64,
    text: &str,
    size: f32,
    colour: Rgba<u8>,
    font_bytes: &[u8],
) -> bool {
    let Ok(font) = fontdue::Font::from_bytes(font_bytes, fontdue::FontSettings::default()) else {
        return false;
    };
    let ascent = font
        .horizontal_line_metrics(size)
        .map(|metrics| metrics.ascent)
        .unwrap_or(size);
    let baseline = origin_y as f32 + ascent;
    let mut pen = origin_x as f32;
    for character in text.chars() {
        let (metrics, coverage) = font.rasterize(character, size);
        if metrics.width > 0 && metrics.height > 0 {
            let left = pen.round() as i64 + i64::from(metrics.xmin);
            let top =
                (baseline - metrics.ymin as f32 - metrics.height as f32).round() as i64;
            for row in 0..metrics.height {
                for column in 0..metrics.width {
                    let alpha = f64::from(coverage[row * metrics.width + column]) / 255.0;
                    if alpha <= 0.0 {
                        continue;
                    }
                    let x = left + column as i64;
                    let y = top + row as i64;
                    if x < 0 || y < 0 || x >= buffer.width() as i64 || y >= buffer.height() as i64
                    {
                        continue;
                    }
                    let existing = buffer.get_pixel(x as u32, y as u32).0;
                    let mut blended = [0u8; 4];
                    for channel in 0..4 {
                        blended[channel] = clamp_byte(
                            f64::from(existing[channel]) * (1.0 - alpha)
                                + f64::from(colour[channel]) * alpha,
                        );
                    }
                    buffer.put_pixel(x as u32, y as u32, Rgba(blended));
                }
            }
        }
        pen += metrics.advance_width;
    }
    true
}

fn show_image(index: i64) -> i64 {
    let Some(image) = get(index) else {
        return -1;
    };
    let path = std::env::temp_dir().join(format!("lynxer-image-{index}.png"));
    if image.save(&path).is_err() {
        return -1;
    }
    let opener = if cfg!(target_os = "macos") { "open" } else { "xdg-open" };
    match std::process::Command::new(opener).arg(&path).spawn() {
        Ok(_) => 0,
        Err(_) => -1,
    }
}

fn kernel_op(index: i64, divisor: f64, kernel: [f64; 9]) -> i64 {
    get(index)
        .map(|image| store(convolve3x3(&image, divisor, kernel), None))
        .unwrap_or(-1)
}

fn rank_op(index: i64, size: i64, pick: fn(&mut Vec<u8>) -> u8) -> i64 {
    get(index)
        .map(|image| store(rank_filter(&image, size, pick), None))
        .unwrap_or(-1)
}

fn draw_op(index: i64, action: impl FnOnce(&mut image::RgbaImage)) -> i64 {
    let Some(image) = get(index) else {
        return -1;
    };
    let mut buffer = image.to_rgba8();
    action(&mut buffer);
    if update(index, DynamicImage::ImageRgba8(buffer), None) {
        0
    } else {
        -1
    }
}

export_int!(image_fit, args, {
    let (width, height) = (args.int(1), args.int(2));
    if width <= 0 || height <= 0 {
        return -1;
    }
    get(args.int(0))
        .map(|image| store(fit_to(image, width as u32, height as u32), None))
        .unwrap_or(-1)
});

export_int!(image_contain, args, {
    let (width, height) = (args.int(1), args.int(2));
    if width <= 0 || height <= 0 {
        return -1;
    }
    let background = color(&args, 3, 255);
    get(args.int(0))
        .map(|image| store(contain_in(image, width as u32, height as u32, background), None))
        .unwrap_or(-1)
});

export_int!(image_color, args, {
    get(args.int(0))
        .map(|image| store(enhance_color(&image, args.num(1)), None))
        .unwrap_or(-1)
});

export_int!(image_sharpness, args, {
    get(args.int(0))
        .map(|image| store(enhance_sharpness(&image, args.num(1)), None))
        .unwrap_or(-1)
});

export_int!(image_smooth, args, {
    kernel_op(
        args.int(0),
        13.0,
        [1.0, 1.0, 1.0, 1.0, 5.0, 1.0, 1.0, 1.0, 1.0],
    )
});
export_int!(image_detail, args, {
    kernel_op(
        args.int(0),
        1.0,
        [0.0, -1.0, 0.0, -1.0, 10.0, -1.0, 0.0, -1.0, 0.0],
    )
});
export_int!(image_edge_enhance, args, {
    kernel_op(
        args.int(0),
        1.0,
        [-1.0, -1.0, -1.0, -1.0, 9.0, -1.0, -1.0, -1.0, -1.0],
    )
});
export_int!(image_find_edges, args, {
    kernel_op(
        args.int(0),
        1.0,
        [-1.0, -1.0, -1.0, -1.0, 8.0, -1.0, -1.0, -1.0, -1.0],
    )
});
export_int!(image_contour, args, {
    kernel_op(
        args.int(0),
        1.0,
        [1.0, 1.0, 1.0, 1.0, -7.0, 1.0, 1.0, 1.0, 1.0],
    )
});
export_int!(image_emboss, args, {
    get(args.int(0))
        .map(|image| {
            store(
                convolve3x3_offset(
                    &image,
                    [-1.0, -1.0, 0.0, -1.0, 1.0, 1.0, 0.0, 1.0, 1.0],
                    128.0,
                ),
                None,
            )
        })
        .unwrap_or(-1)
});

export_int!(image_median_filter, args, {
    rank_op(args.int(0), args.int(1), pick_median)
});
export_int!(image_min_filter, args, {
    rank_op(args.int(0), args.int(1), pick_min)
});
export_int!(image_max_filter, args, {
    rank_op(args.int(0), args.int(1), pick_max)
});

export_int!(image_composite, args, {
    let (Some(base), Some(overlay), Some(mask)) = (
        get(args.int(0)),
        get(args.int(1)),
        get(args.int(2)),
    ) else {
        return -1;
    };
    store(composite_over(&base, &overlay, &mask), None)
});

export_int!(image_auto_contrast, args, {
    get(args.int(0))
        .map(|image| store(auto_contrast(&image), None))
        .unwrap_or(-1)
});
export_int!(image_equalize, args, {
    get(args.int(0))
        .map(|image| store(equalize(&image), None))
        .unwrap_or(-1)
});
export_int!(image_solarize, args, {
    get(args.int(0))
        .map(|image| store(solarize(&image, args.int(1)), None))
        .unwrap_or(-1)
});
export_int!(image_posterize, args, {
    get(args.int(0))
        .map(|image| store(posterize(&image, args.int(1)), None))
        .unwrap_or(-1)
});
export_int!(image_quantize, args, {
    get(args.int(0))
        .map(|image| store(quantize(&image, args.int(1)), None))
        .unwrap_or(-1)
});

export_string!(image_split_channels, args, {
    match get(args.int(0)) {
        Some(image) => split_channels(&image),
        None => "ERROR: unknown image".to_string(),
    }
});

export_int!(image_merge_channels, args, {
    merge_channels([args.int(0), args.int(1), args.int(2), args.int(3)])
        .map(|image| store(image, None))
        .unwrap_or(-1)
});

export_int!(image_draw_line, args, {
    let colour = rgb(&args, 5);
    let width = args.int(8);
    let (x1, y1, x2, y2) = (args.int(1), args.int(2), args.int(3), args.int(4));
    draw_op(args.int(0), move |buffer| {
        draw_line(buffer, x1, y1, x2, y2, colour, width)
    })
});

export_int!(image_draw_rect, args, {
    let colour = rgb(&args, 5);
    // Bools are packed into the numeric list too, so `fill` sits at index 8.
    let fill = args.bool(8);
    let (x1, y1, x2, y2) = (args.int(1), args.int(2), args.int(3), args.int(4));
    draw_op(args.int(0), move |buffer| {
        draw_rect(buffer, x1, y1, x2, y2, colour, fill)
    })
});

export_int!(image_draw_rounded_rect, args, {
    let colour = rgb(&args, 5);
    let fill = args.bool(8);
    let radius = args.int(9);
    let (x1, y1, x2, y2) = (args.int(1), args.int(2), args.int(3), args.int(4));
    draw_op(args.int(0), move |buffer| {
        draw_rounded_rect(buffer, x1, y1, x2, y2, colour, fill, radius)
    })
});

export_int!(image_draw_circle, args, {
    let colour = rgb(&args, 4);
    let fill = args.bool(7);
    let (cx, cy, radius) = (args.int(1), args.int(2), args.int(3));
    draw_op(args.int(0), move |buffer| {
        draw_circle(buffer, cx, cy, radius, colour, fill)
    })
});

export_int!(image_draw_ellipse, args, {
    let colour = rgb(&args, 5);
    let fill = args.bool(8);
    let (x1, y1, x2, y2) = (args.int(1), args.int(2), args.int(3), args.int(4));
    draw_op(args.int(0), move |buffer| {
        draw_ellipse(buffer, x1, y1, x2, y2, colour, fill)
    })
});

export_int!(image_draw_polygon, args, {
    let colour = rgb(&args, 1);
    let points = parse_points(args.string(0));
    draw_op(args.int(0), move |buffer| draw_polygon(buffer, &points, colour))
});

export_int!(image_draw_text, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let Some(font_bytes) = system_font_bytes() else {
        return -1;
    };
    let size = args.num(6) as f32;
    if size <= 0.0 {
        return -1;
    }
    let colour = rgb(&args, 3);
    let text = args.string(0).to_string();
    let (x, y) = (args.int(1), args.int(2));
    let mut buffer = image.to_rgba8();
    if !draw_text_into(&mut buffer, x, y, &text, size, colour, &font_bytes) {
        return -1;
    }
    if update(args.int(0), DynamicImage::ImageRgba8(buffer), None) {
        0
    } else {
        -1
    }
});

export_int!(image_draw_text_font, args, {
    let Some(image) = get(args.int(0)) else {
        return -1;
    };
    let Ok(font_bytes) = std::fs::read(args.string(1)) else {
        return -1;
    };
    let size = args.num(6) as f32;
    if size <= 0.0 {
        return -1;
    }
    let colour = rgb(&args, 3);
    let text = args.string(0).to_string();
    let (x, y) = (args.int(1), args.int(2));
    let mut buffer = image.to_rgba8();
    if !draw_text_into(&mut buffer, x, y, &text, size, colour, &font_bytes) {
        return -1;
    }
    if update(args.int(0), DynamicImage::ImageRgba8(buffer), None) {
        0
    } else {
        -1
    }
});

export_int!(image_flood_fill, args, {
    let colour = rgb(&args, 3);
    let (x, y) = (args.int(1), args.int(2));
    draw_op(args.int(0), move |buffer| flood_fill(buffer, x, y, colour))
});

export_int!(image_show, args, { show_image(args.int(0)) });

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
    ("fit", "image_fit", "cdecl:int64(...)"),
    ("contain", "image_contain", "cdecl:int64(...)"),
    ("color", "image_color", "cdecl:int64(...)"),
    ("sharpness", "image_sharpness", "cdecl:int64(...)"),
    ("smooth", "image_smooth", "cdecl:int64(...)"),
    ("detail", "image_detail", "cdecl:int64(...)"),
    ("edgeEnhance", "image_edge_enhance", "cdecl:int64(...)"),
    ("emboss", "image_emboss", "cdecl:int64(...)"),
    ("findEdges", "image_find_edges", "cdecl:int64(...)"),
    ("contour", "image_contour", "cdecl:int64(...)"),
    ("medianFilter", "image_median_filter", "cdecl:int64(...)"),
    ("minFilter", "image_min_filter", "cdecl:int64(...)"),
    ("maxFilter", "image_max_filter", "cdecl:int64(...)"),
    ("composite", "image_composite", "cdecl:int64(...)"),
    ("autoContrast", "image_auto_contrast", "cdecl:int64(...)"),
    ("equalize", "image_equalize", "cdecl:int64(...)"),
    ("solarize", "image_solarize", "cdecl:int64(...)"),
    ("posterize", "image_posterize", "cdecl:int64(...)"),
    ("quantize", "image_quantize", "cdecl:int64(...)"),
    ("splitChannels", "image_split_channels", "cdecl:cstring(...)"),
    ("mergeChannels", "image_merge_channels", "cdecl:int64(...)"),
    ("drawLine", "image_draw_line", "cdecl:int64(...)"),
    ("drawRect", "image_draw_rect", "cdecl:int64(...)"),
    ("drawRoundedRect", "image_draw_rounded_rect", "cdecl:int64(...)"),
    ("drawCircle", "image_draw_circle", "cdecl:int64(...)"),
    ("drawEllipse", "image_draw_ellipse", "cdecl:int64(...)"),
    ("drawPolygon", "image_draw_polygon", "cdecl:int64(...)"),
    ("drawText", "image_draw_text", "cdecl:int64(...)"),
    ("drawTextFont", "image_draw_text_font", "cdecl:int64(...)"),
    ("floodFill", "image_flood_fill", "cdecl:int64(...)"),
    ("show", "image_show", "cdecl:int64(...)"),
];

lynxer_module!(OPS);
