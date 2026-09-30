//! Software rasterizer for headless mode.
//!
//! Without a window there is no GPU, so a drawing op that would call macroquad
//! rasterizes into an in-memory RGBA framebuffer instead. `screenshot` then has
//! real pixels to write, which is what makes rendering testable in CI.
//!
//! Only the primitives that can be expressed exactly on a CPU pixel grid are
//! implemented — rectangles, lines, circles, triangles and polygons. Anything
//! that needs shaping or a GPU (text, textures, shaders, render targets, the
//! immediate-mode UI) stays a no-op headless and is documented as such.

use macroquad::color::Color;

use crate::state::with;

/// Rounds a macroquad colour channel to the byte the framebuffer stores.
fn channel(value: f32) -> u8 {
    (value.clamp(0.0, 1.0) * 255.0).round() as u8
}

/// The four bytes of a colour, in RGBA order.
pub fn rgba(color: Color) -> [u8; 4] {
    [
        channel(color.r),
        channel(color.g),
        channel(color.b),
        channel(color.a),
    ]
}

/// An RGBA pixel buffer, in the same top-left origin macroquad draws in.
pub struct Framebuffer {
    pub width: usize,
    pub height: usize,
    pub pixels: Vec<u8>,
}

impl Framebuffer {
    pub fn new(width: usize, height: usize) -> Self {
        Framebuffer {
            width: width.max(1),
            height: height.max(1),
            pixels: vec![0; width.max(1) * height.max(1) * 4],
        }
    }

    /// Writes one pixel, ignoring anything outside the frame.
    pub fn set(&mut self, x: i64, y: i64, color: Color) {
        if x < 0 || y < 0 || x >= self.width as i64 || y >= self.height as i64 {
            return;
        }
        let offset = (y as usize * self.width + x as usize) * 4;
        let bytes = rgba(color);
        self.pixels[offset..offset + 4].copy_from_slice(&bytes);
    }

    /// Fills every pixel.
    pub fn clear(&mut self, color: Color) {
        let bytes = rgba(color);
        for pixel in self.pixels.chunks_exact_mut(4) {
            pixel.copy_from_slice(&bytes);
        }
    }

    pub fn rectangle(&mut self, x: f32, y: f32, w: f32, h: f32, color: Color) {
        let left = x.round() as i64;
        let top = y.round() as i64;
        let right = (x + w).round() as i64;
        let bottom = (y + h).round() as i64;
        for py in top..bottom {
            for px in left..right {
                self.set(px, py, color);
            }
        }
    }

    /// A rectangle outline `thickness` pixels wide, drawn inside the rectangle.
    pub fn rectangle_lines(
        &mut self,
        x: f32,
        y: f32,
        w: f32,
        h: f32,
        thickness: f32,
        color: Color,
    ) {
        let t = (thickness.round() as i64).max(1);
        self.rectangle(x, y, w, t as f32, color);
        self.rectangle(x, y + h - t as f32, w, t as f32, color);
        self.rectangle(x, y, t as f32, h, color);
        self.rectangle(x + w - t as f32, y, t as f32, h, color);
    }

    /// A line with a square brush of the given thickness.
    pub fn line(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, thickness: f32, color: Color) {
        let (mut x, mut y) = (x1.round() as i64, y1.round() as i64);
        let (target_x, target_y) = (x2.round() as i64, y2.round() as i64);
        let dx = (target_x - x).abs();
        let dy = -(target_y - y).abs();
        let step_x = if x < target_x { 1 } else { -1 };
        let step_y = if y < target_y { 1 } else { -1 };
        let mut error = dx + dy;
        let half = ((thickness.round() as i64).max(1) - 1) / 2;
        loop {
            for oy in -half..=half {
                for ox in -half..=half {
                    self.set(x + ox, y + oy, color);
                }
            }
            if x == target_x && y == target_y {
                break;
            }
            let doubled = 2 * error;
            if doubled >= dy {
                error += dy;
                x += step_x;
            }
            if doubled <= dx {
                error += dx;
                y += step_y;
            }
        }
    }

    /// A filled circle, at the given centre and radius, in pixels.
    pub fn circle(&mut self, cx: f32, cy: f32, radius: f32, color: Color) {
        let r = radius.max(0.0);
        let limit = (r + 0.5).floor() as i64;
        for oy in -limit..=limit {
            for ox in -limit..=limit {
                let (fx, fy) = (ox as f32, oy as f32);
                if (fx * fx + fy * fy).sqrt() <= r {
                    self.set(cx.round() as i64 + ox, cy.round() as i64 + oy, color);
                }
            }
        }
    }

    /// A circle outline, drawn between `radius - thickness` and `radius`.
    pub fn circle_lines(
        &mut self,
        cx: f32,
        cy: f32,
        radius: f32,
        thickness: f32,
        color: Color,
    ) {
        let outer = radius.max(0.0);
        let inner = (outer - thickness.max(1.0)).max(0.0);
        let limit = (outer + 0.5).floor() as i64;
        for oy in -limit..=limit {
            for ox in -limit..=limit {
                let (fx, fy) = (ox as f32, oy as f32);
                let distance = (fx * fx + fy * fy).sqrt();
                if distance <= outer && (distance >= inner || inner == 0.0) {
                    self.set(cx.round() as i64 + ox, cy.round() as i64 + oy, color);
                }
            }
        }
    }

    /// A filled triangle (barycentric coverage of each pixel centre).
    pub fn triangle(
        &mut self,
        x1: f32,
        y1: f32,
        x2: f32,
        y2: f32,
        x3: f32,
        y3: f32,
        color: Color,
    ) {
        self.polygon(&[(x1, y1), (x2, y2), (x3, y3)], color);
    }

    /// A filled polygon, by the even-odd rule: a pixel is inside when a ray
    /// from it crosses the outline an odd number of times.
    pub fn polygon(&mut self, points: &[(f32, f32)], color: Color) {
        if points.len() < 3 {
            return;
        }
        let mut top = f32::MAX;
        let mut bottom = f32::MIN;
        let mut left = f32::MAX;
        let mut right = f32::MIN;
        for (x, y) in points {
            top = top.min(*y);
            bottom = bottom.max(*y);
            left = left.min(*x);
            right = right.max(*x);
        }
        let first_y = (top.floor().max(0.0)) as i64;
        let last_y = (bottom.ceil().min(self.height as f32)) as i64;
        let first_x = (left.floor().max(0.0)) as i64;
        let last_x = (right.ceil().min(self.width as f32)) as i64;
        for py in first_y..last_y {
            let sample_y = py as f32 + 0.5;
            for px in first_x..last_x {
                let sample_x = px as f32 + 0.5;
                if point_in_polygon(sample_x, sample_y, points) {
                    self.set(px, py, color);
                }
            }
        }
    }
}

/// The even-odd inside test used by [`Framebuffer::polygon`].
fn point_in_polygon(x: f32, y: f32, points: &[(f32, f32)]) -> bool {
    let mut inside = false;
    let mut previous = points.len() - 1;
    for current in 0..points.len() {
        let (xi, yi) = points[current];
        let (xj, yj) = points[previous];
        if (yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi {
            inside = !inside;
        }
        previous = current;
    }
    inside
}

/// Runs `body` against the headless framebuffer, creating or resizing it first
/// when the window size changed. Does nothing when a window is open.
pub fn with_framebuffer(body: impl FnOnce(&mut Framebuffer)) {
    with(|state| {
        if !state.headless {
            return;
        }
        let width = state.width.max(1.0) as usize;
        let height = state.height.max(1.0) as usize;
        let needs_resize = match &state.framebuffer {
            Some(buffer) => buffer.width != width || buffer.height != height,
            None => true,
        };
        if needs_resize {
            state.framebuffer = Some(Framebuffer::new(width, height));
        }
        if let Some(buffer) = state.framebuffer.as_mut() {
            body(buffer);
        }
    });
}

/// The frame's pixels as an RGBA byte vector, for `screenshot`.
///
/// `Image::export_png` flips the rows, because macroquad stores textures
/// bottom-up. Pre-flipping here cancels that, so the written PNG is in the same
/// top-left origin the drawing ops use.
pub fn image_bytes() -> Option<(Vec<u8>, u16, u16)> {
    with(|state| {
        state.framebuffer.as_ref().map(|buffer| {
            let stride = buffer.width * 4;
            let mut bytes = buffer.pixels.clone();
            for row in 0..buffer.height / 2 {
                let top = row * stride;
                let bottom = (buffer.height - 1 - row) * stride;
                for offset in 0..stride {
                    bytes.swap(top + offset, bottom + offset);
                }
            }
            (bytes, buffer.width as u16, buffer.height as u16)
        })
    })
}
