//! Headless text rendering.
//!
//! With no window there is no glyph atlas texture, so text is rasterized on the
//! CPU with `fontdue`. The default font is a TTF bundled with the crate (the
//! same ProggyClean face macroquad embeds), so `drawText` has something to draw
//! without a font file; `loadFont` rasterizes a user-supplied TTF the same way.
//! Anti-aliasing is fontdue's, so headless text is a close visual match to the
//! GPU path rather than a pixel-identical one.

use std::sync::{Arc, OnceLock};

use macroquad::color::Color;

use crate::raster::Framebuffer;

/// A font parsed once, rasterized per glyph on demand. Cloning shares the
/// parsed face, so a handle can be resolved before the framebuffer is borrowed.
#[derive(Clone)]
pub struct RasterFont {
    face: Arc<fontdue::Font>,
}

impl RasterFont {
    pub fn from_bytes(bytes: &[u8]) -> Option<RasterFont> {
        fontdue::Font::from_bytes(bytes, fontdue::FontSettings::default())
            .ok()
            .map(|face| RasterFont {
                face: Arc::new(face),
            })
    }

    /// The advance width and line height of `text` at `px` pixels.
    pub fn measure(&self, text: &str, px: f32) -> (f32, f32) {
        let px = px.max(1.0);
        let mut width = 0.0f32;
        let mut previous: Option<char> = None;
        for character in text.chars() {
            if let Some(before) = previous {
                if let Some(kern) = self.face.horizontal_kern(before, character, px) {
                    width += kern;
                }
            }
            width += self.face.metrics(character, px).advance_width;
            previous = Some(character);
        }
        let height = self
            .face
            .horizontal_line_metrics(px)
            .map(|line| line.new_line_size)
            .unwrap_or(px);
        (width, height)
    }

    /// Draws `text` with `(x, y)` as the baseline origin, rotated about it.
    pub fn draw(
        &self,
        framebuffer: &mut Framebuffer,
        text: &str,
        x: f32,
        y: f32,
        px: f32,
        rotation: f32,
        color: Color,
    ) {
        let px = px.max(1.0);
        let (sin, cos) = rotation.sin_cos();
        let mut pen = 0.0f32;
        let mut previous: Option<char> = None;
        for character in text.chars() {
            if let Some(before) = previous {
                if let Some(kern) = self.face.horizontal_kern(before, character, px) {
                    pen += kern;
                }
            }
            let (metrics, bitmap) = self.face.rasterize(character, px);
            if metrics.width > 0 && metrics.height > 0 {
                let glyph_x = pen + metrics.xmin as f32;
                // fontdue's ymin is positive up from the baseline; the bitmap
                // top is `ymin + height` above it, i.e. that far above the
                // baseline on screen.
                let glyph_y = -(metrics.ymin as f32) - metrics.height as f32;
                for row in 0..metrics.height {
                    for column in 0..metrics.width {
                        let coverage = bitmap[row * metrics.width + column];
                        if coverage == 0 {
                            continue;
                        }
                        let local_x = glyph_x + column as f32;
                        let local_y = glyph_y + row as f32;
                        let world_x = x + local_x * cos - local_y * sin;
                        let world_y = y + local_x * sin + local_y * cos;
                        let alpha = color.a * coverage as f32 / 255.0;
                        framebuffer.blend(
                            world_x.round() as i64,
                            world_y.round() as i64,
                            Color::new(color.r, color.g, color.b, alpha),
                        );
                    }
                }
            }
            pen += metrics.advance_width;
            previous = Some(character);
        }
    }
}

static BUILTIN: OnceLock<RasterFont> = OnceLock::new();

/// The bundled default font, parsed once.
pub fn builtin() -> &'static RasterFont {
    BUILTIN.get_or_init(|| {
        RasterFont::from_bytes(include_bytes!("../assets/ProggyClean.ttf"))
            .expect("bundled ProggyClean font must parse")
    })
}

/// Greedy word wrap at `max_width`, the way `wrap_text` behaves for a single
/// font. Words longer than the limit stay whole on their own line.
pub fn wrap(font: &RasterFont, text: &str, px: f32, max_width: f32) -> String {
    if max_width <= 0.0 {
        return text.to_string();
    }
    let mut lines: Vec<String> = Vec::new();
    let mut current = String::new();
    for word in text.split_whitespace() {
        if current.is_empty() {
            current.push_str(word);
            continue;
        }
        let candidate = format!("{current} {word}");
        if font.measure(&candidate, px).0 <= max_width {
            current = candidate;
        } else {
            lines.push(std::mem::take(&mut current));
            current.push_str(word);
        }
    }
    if !current.is_empty() {
        lines.push(current);
    }
    lines.join("\n")
}
