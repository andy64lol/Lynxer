//! Randomness, colour-space conversion and the asset folder.

use macroquad::color::{hsl_to_rgb, rgb_to_hsl, Color};
use macroquad::rand::{gen_range, rand, srand};

use crate::state::{number, with};

pub fn seed(value: i64) {
    srand(value as u64);
}

pub fn next() -> i64 {
    rand() as i64
}

pub fn range(low: f32, high: f32) -> f32 {
    if high <= low {
        return low;
    }
    gen_range(low, high)
}

pub fn range_int(low: i64, high: i64) -> i64 {
    if high <= low {
        return low;
    }
    gen_range(low, high)
}

pub fn from_hsl(h: f32, s: f32, l: f32) -> Color {
    hsl_to_rgb(h, s, l)
}

/// `[h,s,l]` for the given 0..255 channels.
pub fn to_hsl(r: i64, g: i64, b: i64) -> String {
    let color = crate::state::color_of(r, g, b);
    let (h, s, l) = rgb_to_hsl(color);
    format!("[{},{},{}]", number(h), number(s), number(l))
}

pub fn set_assets_folder(path: &str) {
    if path.is_empty() || with(|state| state.headless) {
        return;
    }
    macroquad::file::set_pc_assets_folder(path);
}

/// Parses a flat list of numbers from a JSON-ish string (`[1,2]`, `"[1, 2]"`).
/// Any character that cannot continue a number separates tokens.
pub fn parse_floats(text: &str) -> Vec<f32> {
    let mut values = Vec::new();
    let mut current = String::new();
    for character in text.chars() {
        let is_digit = character.is_ascii_digit();
        let is_sign = character == '-' || character == '+';
        let is_dot = character == '.';
        if current.is_empty() {
            if is_digit || is_sign || is_dot {
                current.push(character);
            }
        } else if is_digit || is_dot {
            current.push(character);
        } else {
            if let Ok(value) = current.parse::<f32>() {
                values.push(value);
            }
            current.clear();
            if is_sign || is_dot {
                current.push(character);
            }
        }
    }
    if !current.is_empty() {
        if let Ok(value) = current.parse::<f32>() {
            values.push(value);
        }
    }
    values
}
