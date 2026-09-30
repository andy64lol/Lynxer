//! Text: font loading, drawing, measuring and wrapping.
//!
//! Fonts load synchronously (`std::fs` + the TTF parser) so no async plumbing
//! is needed. With a window, macroquad owns the glyph atlas and draws through
//! OpenGL; headless, glyphs are rasterized on the CPU with `fontdue` into the
//! software framebuffer and a font handle indexes the CPU font registry. A
//! handle of `-1` (or an unknown one) selects the built-in ProggyClean face,
//! which is macroquad's default font too.

use macroquad::color::Color;
use macroquad::text::{
    draw_multiline_text, draw_multiline_text_ex, draw_text, draw_text_ex, get_text_center,
    measure_multiline_text, measure_text, set_default_font, wrap_text, Font, TextParams,
};

use crate::font::RasterFont;
use crate::state::{number, with};

fn headless() -> bool {
    with(|state| state.headless)
}

/// The headless font for a handle: the registered CPU font, or the built-in
/// default face when the handle is unknown.
fn raster_font(handle: i64) -> RasterFont {
    with(|state| state.cpu_font(handle).cloned()).unwrap_or_else(|| crate::font::builtin().clone())
}

fn gpu_font(handle: i64) -> Option<Font> {
    with(|state| state.font(handle).cloned())
}

/// Loads a TTF/OTF font. With a window the glyph atlas is a GPU texture; in
/// headless mode the face is parsed for CPU rasterization. `-1` on failure.
pub fn load_font(path: &str) -> i64 {
    if path.is_empty() {
        return -1;
    }
    let bytes = match std::fs::read(path) {
        Ok(bytes) => bytes,
        Err(_) => return -1,
    };
    if headless() {
        match RasterFont::from_bytes(&bytes) {
            Some(font) => with(|state| {
                state.cpu_fonts.push(Some(font));
                (state.cpu_fonts.len() - 1) as i64
            }),
            None => -1,
        }
    } else {
        match macroquad::text::load_ttf_font_from_bytes(&bytes) {
            Ok(font) => with(|state| {
                state.fonts.push(font);
                (state.fonts.len() - 1) as i64
            }),
            Err(_) => -1,
        }
    }
}

pub fn set_default(handle: i64) -> i64 {
    // Headless draws through `font_at`, which already falls back to the
    // built-in face for `-1`; only the GPU path keeps a default font.
    if headless() {
        return if with(|state| state.cpu_font(handle).is_some()) {
            0
        } else {
            -1
        };
    }
    match gpu_font(handle) {
        Some(font) => {
            set_default_font(font);
            0
        }
        None => -1,
    }
}

pub fn text(content: &str, x: f32, y: f32, size: f32, color: Color) {
    if headless() {
        let font = raster_font(-1);
        crate::raster::with_framebuffer(|buffer| {
            font.draw(buffer, content, x, y, size, 0.0, color);
        });
        return;
    }
    draw_text(content, x, y, size, color);
}

#[allow(clippy::too_many_arguments)]
pub fn text_ex(
    handle: i64,
    content: &str,
    x: f32,
    y: f32,
    size: f32,
    scale: f32,
    rotation: f32,
    color: Color,
) {
    let scale = if scale > 0.0 { scale } else { 1.0 };
    if headless() {
        let font = raster_font(handle);
        let px = (size.max(1.0)) * scale;
        crate::raster::with_framebuffer(|buffer| {
            font.draw(buffer, content, x, y, px, rotation, color);
        });
        return;
    }
    let font = gpu_font(handle);
    draw_text_ex(
        content,
        x,
        y,
        TextParams {
            font: font.as_ref(),
            font_size: size.max(1.0) as u16,
            font_scale: scale,
            font_scale_aspect: 1.0,
            rotation,
            color,
        },
    );
}

fn line_distance(value: f32) -> Option<f32> {
    if value > 0.0 {
        Some(value)
    } else {
        None
    }
}

pub fn multiline(content: &str, x: f32, y: f32, size: f32, separation: f32, color: Color) {
    if headless() {
        let font = raster_font(-1);
        let line_height = if separation > 0.0 {
            separation
        } else {
            font.measure("", size.max(1.0)).1
        };
        crate::raster::with_framebuffer(|buffer| {
            for (index, line) in content.lines().enumerate() {
                let baseline = y + index as f32 * line_height;
                font.draw(buffer, line, x, baseline, size, 0.0, color);
            }
        });
        return;
    }
    draw_multiline_text(content, x, y, size, line_distance(separation), color);
}

#[allow(clippy::too_many_arguments)]
pub fn multiline_ex(
    handle: i64,
    content: &str,
    x: f32,
    y: f32,
    size: f32,
    separation: f32,
    scale: f32,
    rotation: f32,
    color: Color,
) {
    let scale = if scale > 0.0 { scale } else { 1.0 };
    if headless() {
        let font = raster_font(handle);
        let px = size.max(1.0) * scale;
        let line_height = if separation > 0.0 {
            separation
        } else {
            font.measure("", px).1
        };
        crate::raster::with_framebuffer(|buffer| {
            for (index, line) in content.lines().enumerate() {
                let baseline = y + index as f32 * line_height;
                font.draw(buffer, line, x, baseline, px, rotation, color);
            }
        });
        return;
    }
    let font = gpu_font(handle);
    draw_multiline_text_ex(
        content,
        x,
        y,
        line_distance(separation),
        TextParams {
            font: font.as_ref(),
            font_size: size.max(1.0) as u16,
            font_scale: scale,
            font_scale_aspect: 1.0,
            rotation,
            color,
        },
    );
}

fn dimensions_json(width: f32, height: f32, offset_y: f32) -> String {
    format!(
        "{{\"width\":{},\"height\":{},\"offsetY\":{}}}",
        number(width),
        number(height),
        number(offset_y)
    )
}

pub fn measure(content: &str, handle: i64, size: f32, scale: f32) -> String {
    let scale = if scale > 0.0 { scale } else { 1.0 };
    if headless() {
        let font = raster_font(handle);
        let (width, height) = font.measure(content, size.max(1.0) * scale);
        return dimensions_json(width, height, 0.0);
    }
    let font = gpu_font(handle);
    let dimensions = measure_text(content, font.as_ref(), size.max(1.0) as u16, scale);
    dimensions_json(dimensions.width, dimensions.height, dimensions.offset_y)
}

pub fn measure_width(content: &str, handle: i64, size: f32, scale: f32) -> f32 {
    let scale = if scale > 0.0 { scale } else { 1.0 };
    if headless() {
        return raster_font(handle)
            .measure(content, size.max(1.0) * scale)
            .0;
    }
    let font = gpu_font(handle);
    measure_text(content, font.as_ref(), size.max(1.0) as u16, scale).width
}

pub fn measure_height(content: &str, handle: i64, size: f32, scale: f32) -> f32 {
    let scale = if scale > 0.0 { scale } else { 1.0 };
    if headless() {
        return raster_font(handle)
            .measure(content, size.max(1.0) * scale)
            .1;
    }
    let font = gpu_font(handle);
    measure_text(content, font.as_ref(), size.max(1.0) as u16, scale).height
}

pub fn measure_multiline(
    content: &str,
    handle: i64,
    size: f32,
    scale: f32,
    max_width: f32,
) -> String {
    let scale = if scale > 0.0 { scale } else { 1.0 };
    if headless() {
        let font = raster_font(handle);
        let px = size.max(1.0) * scale;
        let line_height = font.measure("", px).1;
        let mut width = 0.0f32;
        let mut lines = 0.0f32;
        for line in content.lines() {
            width = width.max(font.measure(line, px).0);
            lines += 1.0;
        }
        let _ = max_width;
        return dimensions_json(width, line_height * lines.max(1.0), 0.0);
    }
    let font = gpu_font(handle);
    let dimensions = measure_multiline_text(
        content,
        font.as_ref(),
        size.max(1.0) as u16,
        scale,
        line_distance(max_width),
    );
    dimensions_json(dimensions.width, dimensions.height, dimensions.offset_y)
}

pub fn center(content: &str, handle: i64, size: f32, scale: f32, rotation: f32) -> String {
    let scale = if scale > 0.0 { scale } else { 1.0 };
    if headless() {
        let font = raster_font(handle);
        let (width, height) = font.measure(content, size.max(1.0) * scale);
        // The text is drawn from a baseline at the origin, so its centre is up
        // and to the right of the origin.
        return format!("[{},{}]", number(width / 2.0), number(-height / 2.0));
    }
    let font = gpu_font(handle);
    let point = get_text_center(
        content,
        font.as_ref(),
        size.max(1.0) as u16,
        scale,
        rotation,
    );
    format!("[{},{}]", number(point.x), number(point.y))
}

pub fn wrap(content: &str, handle: i64, size: f32, max_width: f32) -> String {
    if headless() {
        return crate::font::wrap(&raster_font(handle), content, size.max(1.0), max_width);
    }
    let font = gpu_font(handle);
    wrap_text(content, font.as_ref(), size.max(1.0) as u16, 1.0, max_width)
}
