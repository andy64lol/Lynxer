//! Text: font loading, drawing, measuring and wrapping.
//!
//! Fonts load synchronously (`std::fs` + `load_ttf_font_from_bytes`) so no
//! async plumbing is needed, and every op is a no-op or zero result in headless
//! mode.

use macroquad::color::Color;
use macroquad::text::{
    draw_multiline_text, draw_multiline_text_ex, draw_text, draw_text_ex, get_text_center,
    measure_multiline_text, measure_text, set_default_font, wrap_text, Font, TextParams,
};

use crate::state::{number, with};

fn headless() -> bool {
    with(|state| state.headless)
}

/// Loads a TTF/OTF font. Requires the GL context (the glyph atlas is a texture),
/// so it only works from inside a callback and never in headless mode.
pub fn load_font(path: &str) -> Option<Font> {
    if headless() || path.is_empty() {
        return None;
    }
    let bytes = std::fs::read(path).ok()?;
    macroquad::text::load_ttf_font_from_bytes(&bytes).ok()
}

pub fn set_default(font: &Font) {
    if headless() {
        return;
    }
    set_default_font(font.clone());
}

pub fn text(text: &str, x: f32, y: f32, size: f32, color: Color) {
    if headless() {
        return;
    }
    draw_text(text, x, y, size, color);
}

pub fn text_ex(
    font: Option<&Font>,
    text: &str,
    x: f32,
    y: f32,
    size: f32,
    scale: f32,
    rotation: f32,
    color: Color,
) {
    if headless() {
        return;
    }
    draw_text_ex(
        text,
        x,
        y,
        TextParams {
            font,
            font_size: size.max(1.0) as u16,
            font_scale: if scale > 0.0 { scale } else { 1.0 },
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

pub fn multiline(text: &str, x: f32, y: f32, size: f32, separation: f32, color: Color) {
    if headless() {
        return;
    }
    draw_multiline_text(text, x, y, size, line_distance(separation), color);
}

pub fn multiline_ex(
    font: Option<&Font>,
    text: &str,
    x: f32,
    y: f32,
    size: f32,
    separation: f32,
    scale: f32,
    rotation: f32,
    color: Color,
) {
    if headless() {
        return;
    }
    draw_multiline_text_ex(
        text,
        x,
        y,
        line_distance(separation),
        TextParams {
            font,
            font_size: size.max(1.0) as u16,
            font_scale: if scale > 0.0 { scale } else { 1.0 },
            font_scale_aspect: 1.0,
            rotation,
            color,
        },
    );
}

pub fn measure(text: &str, font: Option<&Font>, size: f32, scale: f32) -> String {
    if headless() {
        return "{\"width\":0,\"height\":0,\"offsetY\":0}".to_string();
    }
    let dimensions = measure_text(text, font, size.max(1.0) as u16, scale);
    format!(
        "{{\"width\":{},\"height\":{},\"offsetY\":{}}}",
        number(dimensions.width),
        number(dimensions.height),
        number(dimensions.offset_y)
    )
}

pub fn measure_width(text: &str, font: Option<&Font>, size: f32, scale: f32) -> f32 {
    if headless() {
        return 0.0;
    }
    measure_text(text, font, size.max(1.0) as u16, scale).width
}

pub fn measure_height(text: &str, font: Option<&Font>, size: f32, scale: f32) -> f32 {
    if headless() {
        return 0.0;
    }
    measure_text(text, font, size.max(1.0) as u16, scale).height
}

pub fn measure_multiline(
    text: &str,
    font: Option<&Font>,
    size: f32,
    scale: f32,
    max_width: f32,
) -> String {
    if headless() {
        return "{\"width\":0,\"height\":0,\"offsetY\":0}".to_string();
    }
    let dimensions = measure_multiline_text(
        text,
        font,
        size.max(1.0) as u16,
        scale,
        line_distance(max_width),
    );
    format!(
        "{{\"width\":{},\"height\":{},\"offsetY\":{}}}",
        number(dimensions.width),
        number(dimensions.height),
        number(dimensions.offset_y)
    )
}

pub fn center(text: &str, font: Option<&Font>, size: f32, scale: f32, rotation: f32) -> String {
    if headless() {
        return "[0,0]".to_string();
    }
    let point = get_text_center(text, font, size.max(1.0) as u16, scale, rotation);
    format!("[{},{}]", number(point.x), number(point.y))
}

pub fn wrap(text: &str, font: Option<&Font>, size: f32, max_width: f32) -> String {
    if headless() {
        return String::new();
    }
    wrap_text(text, font, size.max(1.0) as u16, 1.0, max_width)
}
