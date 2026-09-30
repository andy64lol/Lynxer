//! Software rasterizer for headless mode.
//!
//! Without a window there is no GPU, so a drawing op that would call macroquad
//! rasterizes into an in-memory RGBA framebuffer instead. `screenshot` then has
//! real pixels to write, which is what makes rendering testable in CI.
//!
//! Every shape primitive that can be expressed on a CPU pixel grid is
//! implemented here — rectangles (also rotated), lines, circles, ellipses,
//! polygons (also the regular `drawPoly`/`drawHexagon` forms), triangles, arcs,
//! text through the built-in bitmap font, and texture blits. A GPU-only op
//! (a fragment shader, a render target's shader pass, a model upload) stays a
//! no-op headless and is documented as such.

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
    pub fn circle_lines(&mut self, cx: f32, cy: f32, radius: f32, thickness: f32, color: Color) {
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

    /// Blends one pixel using the source alpha, so a partially-transparent
    /// texture or glyph composites over what is already drawn.
    pub fn blend(&mut self, x: i64, y: i64, color: Color) {
        if x < 0 || y < 0 || x >= self.width as i64 || y >= self.height as i64 {
            return;
        }
        let offset = (y as usize * self.width + x as usize) * 4;
        let source = rgba(color);
        let alpha = source[3] as u32;
        if alpha == 0 {
            return;
        }
        if alpha == 255 {
            self.pixels[offset..offset + 4].copy_from_slice(&source);
            return;
        }
        let inverse = 255 - alpha;
        for channel in 0..4 {
            let under = self.pixels[offset + channel] as u32;
            self.pixels[offset + channel] =
                ((source[channel] as u32 * alpha + under * inverse) / 255) as u8;
        }
    }

    /// Blits a source RGBA image into a destination rectangle, multiplying each
    /// sample by `color` (tint and alpha). A source sub-rectangle may be given;
    /// `rotation` (radians, about `pivot`) rotates the destination rectangle.
    #[allow(clippy::too_many_arguments)]
    pub fn blit(
        &mut self,
        source: &[u8],
        source_width: usize,
        source_height: usize,
        x: f32,
        y: f32,
        w: f32,
        h: f32,
        source_rect: Option<(f32, f32, f32, f32)>,
        rotation: f32,
        pivot_x: f32,
        pivot_y: f32,
        color: Color,
    ) {
        if source_width == 0 || source_height == 0 || w == 0.0 || h == 0.0 {
            return;
        }
        let (sx, sy, sw, sh) =
            source_rect.unwrap_or((0.0, 0.0, source_width as f32, source_height as f32));
        let (sin, cos) = rotation.sin_cos();
        let corners = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)];
        let mut left = f32::MAX;
        let mut top = f32::MAX;
        let mut right = f32::MIN;
        let mut bottom = f32::MIN;
        for (px, py) in corners {
            let dx = px - pivot_x;
            let dy = py - pivot_y;
            let rx = pivot_x + dx * cos - dy * sin;
            let ry = pivot_y + dx * sin + dy * cos;
            left = left.min(rx);
            top = top.min(ry);
            right = right.max(rx);
            bottom = bottom.max(ry);
        }
        let first_x = (left.floor() as i64).max(0);
        let last_x = (right.ceil() as i64).min(self.width as i64);
        let first_y = (top.floor() as i64).max(0);
        let last_y = (bottom.ceil() as i64).min(self.height as i64);
        let tint = rgba(color);
        for py in first_y..last_y {
            for px in first_x..last_x {
                let mut sample_x = px as f32 + 0.5;
                let mut sample_y = py as f32 + 0.5;
                if rotation != 0.0 {
                    let dx = sample_x - pivot_x;
                    let dy = sample_y - pivot_y;
                    sample_x = pivot_x + dx * cos + dy * sin;
                    sample_y = pivot_y - dx * sin + dy * cos;
                }
                let u = (sample_x - x) / w;
                let v = (sample_y - y) / h;
                if !(0.0..1.0).contains(&u) || !(0.0..1.0).contains(&v) {
                    continue;
                }
                let tex_x = (sx + u * sw) as usize;
                let tex_y = (sy + v * sh) as usize;
                if tex_x >= source_width || tex_y >= source_height {
                    continue;
                }
                let offset = (tex_y * source_width + tex_x) * 4;
                let texel = [
                    source[offset] as u32,
                    source[offset + 1] as u32,
                    source[offset + 2] as u32,
                    source[offset + 3] as u32,
                ];
                let alpha = texel[3] * tint[3] as u32 / 255;
                let blended = Color::new(
                    (texel[0] * tint[0] as u32) as f32 / (255.0 * 255.0),
                    (texel[1] * tint[1] as u32) as f32 / (255.0 * 255.0),
                    (texel[2] * tint[2] as u32) as f32 / (255.0 * 255.0),
                    alpha as f32 / 255.0,
                );
                self.blend(px, py, blended);
            }
        }
    }

    /// A filled ellipse with the given radii, rotated by `rotation` radians.
    pub fn ellipse(&mut self, cx: f32, cy: f32, rx: f32, ry: f32, rotation: f32, color: Color) {
        let (rx, ry) = (rx.abs().max(0.0), ry.abs().max(0.0));
        if rx == 0.0 || ry == 0.0 {
            return;
        }
        let (sin, cos) = rotation.sin_cos();
        let limit = (rx.max(ry) + 1.0).ceil() as i64;
        for oy in -limit..=limit {
            for ox in -limit..=limit {
                let (fx, fy) = (ox as f32, oy as f32);
                // Rotate the sample back into the ellipse's own frame.
                let lx = fx * cos + fy * sin;
                let ly = -fx * sin + fy * cos;
                if (lx / rx).powi(2) + (ly / ry).powi(2) <= 1.0 {
                    self.set(cx.round() as i64 + ox, cy.round() as i64 + oy, color);
                }
            }
        }
    }

    /// An ellipse outline: the boundary drawn as `sides` line segments, the way
    /// macroquad approximates it.
    pub fn ellipse_lines(
        &mut self,
        cx: f32,
        cy: f32,
        rx: f32,
        ry: f32,
        rotation: f32,
        thickness: f32,
        color: Color,
    ) {
        let (rx, ry) = (rx.abs().max(0.0), ry.abs().max(0.0));
        if rx == 0.0 || ry == 0.0 {
            return;
        }
        let sides = 20;
        let (sin, cos) = rotation.sin_cos();
        let point = |index: i64| {
            let angle = std::f32::consts::TAU * index as f32 / sides as f32;
            let (px, py) = (rx * angle.cos(), ry * angle.sin());
            (cx + px * cos - py * sin, cy + py * cos + px * sin)
        };
        for index in 0..sides {
            let (x1, y1) = point(index);
            let (x2, y2) = point(index + 1);
            self.line(x1, y1, x2, y2, thickness, color);
        }
    }

    /// A thick arc: a ring segment between `radius` and `radius + thickness`,
    /// swept by `sweep` radians from `rotation`, the way macroquad draws it.
    pub fn arc(
        &mut self,
        cx: f32,
        cy: f32,
        radius: f32,
        rotation: f32,
        thickness: f32,
        sweep: f32,
        color: Color,
    ) {
        let inner = radius.max(0.0);
        let outer = inner + thickness.max(0.0);
        if outer == 0.0 || sweep == 0.0 {
            return;
        }
        let steps = ((sweep.abs() / std::f32::consts::TAU * 40.0).ceil() as i64).clamp(8, 512);
        let mut outline: Vec<(f32, f32)> = Vec::with_capacity((steps as usize + 1) * 2);
        for step in 0..=steps {
            let angle = rotation + sweep * step as f32 / steps as f32;
            outline.push((cx + inner * angle.cos(), cy + inner * angle.sin()));
        }
        for step in (0..=steps).rev() {
            let angle = rotation + sweep * step as f32 / steps as f32;
            outline.push((cx + outer * angle.cos(), cy + outer * angle.sin()));
        }
        self.polygon(&outline, color);
    }

    /// A filled regular polygon: `sides` vertices at `radius`, rotated by
    /// `rotation` radians.
    pub fn poly(&mut self, cx: f32, cy: f32, sides: i64, radius: f32, rotation: f32, color: Color) {
        let sides = sides.clamp(3, 255);
        let radius = radius.max(0.0);
        let mut points = Vec::with_capacity(sides as usize);
        for index in 0..sides {
            let angle = rotation + std::f32::consts::TAU * index as f32 / sides as f32;
            points.push((cx + radius * angle.cos(), cy + radius * angle.sin()));
        }
        self.polygon(&points, color);
    }

    /// The outline of a regular polygon. macroquad implements this as a full
    /// 360° thick arc, so the outline is a ring at the polygon radius.
    pub fn poly_lines(
        &mut self,
        cx: f32,
        cy: f32,
        sides: i64,
        radius: f32,
        rotation: f32,
        thickness: f32,
        color: Color,
    ) {
        let _ = sides;
        self.arc(
            cx,
            cy,
            radius,
            rotation,
            thickness,
            std::f32::consts::TAU,
            color,
        );
    }

    /// A filled hexagon. `vertical` selects a flat- or point-topped orientation,
    /// matching macroquad.
    pub fn hexagon(&mut self, cx: f32, cy: f32, size: f32, vertical: bool, color: Color) {
        let size = size.max(0.0);
        let rotation = if vertical {
            0.0
        } else {
            std::f32::consts::FRAC_PI_2
        };
        self.poly(cx, cy, 6, size, rotation, color);
    }

    /// The four corners of a rectangle rotated about `(x, y)`, which is the
    /// point `origin` selects inside the rectangle. This mirrors macroquad's
    /// `translate(x,y) * rotate * scale(w,h) * (unit - origin)` transform.
    fn rotated_corners(
        x: f32,
        y: f32,
        w: f32,
        h: f32,
        rotation: f32,
        origin_x: f32,
        origin_y: f32,
    ) -> [(f32, f32); 4] {
        let (sin, cos) = rotation.sin_cos();
        let units = [(0.0f32, 0.0f32), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)];
        let mut rotated = [(0.0f32, 0.0f32); 4];
        for (index, (ux, uy)) in units.iter().enumerate() {
            let lx = w * (ux - origin_x);
            let ly = h * (uy - origin_y);
            rotated[index] = (x + lx * cos - ly * sin, y + lx * sin + ly * cos);
        }
        rotated
    }

    /// A filled rectangle rotated about a pivot given as a fraction of its size.
    pub fn rectangle_rotated(
        &mut self,
        x: f32,
        y: f32,
        w: f32,
        h: f32,
        rotation: f32,
        origin_x: f32,
        origin_y: f32,
        color: Color,
    ) {
        let corners = Self::rotated_corners(x, y, w, h, rotation, origin_x, origin_y);
        self.polygon(&corners, color);
    }

    /// A rotated rectangle outline, `thickness` pixels wide.
    pub fn rectangle_lines_rotated(
        &mut self,
        x: f32,
        y: f32,
        w: f32,
        h: f32,
        thickness: f32,
        rotation: f32,
        origin_x: f32,
        origin_y: f32,
        color: Color,
    ) {
        let corners = Self::rotated_corners(x, y, w, h, rotation, origin_x, origin_y);
        for index in 0..4 {
            let (x1, y1) = corners[index];
            let (x2, y2) = corners[(index + 1) % 4];
            self.line(x1, y1, x2, y2, thickness, color);
        }
    }

    /// A filled triangle (barycentric coverage of each pixel centre).
    pub fn triangle(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x3: f32, y3: f32, color: Color) {
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
        // Drawing is redirected into a render target while one is bound.
        if let Some(handle) = state.active_target {
            if let Some(buffer) = state
                .cpu_targets
                .get_mut(handle as usize)
                .and_then(|target| target.as_mut())
            {
                body(buffer);
                return;
            }
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
/// The frame's pixels in the framebuffer's own top-left origin, unflipped.
pub fn raw_image_bytes() -> Option<(Vec<u8>, u16, u16)> {
    with(|state| {
        state.framebuffer.as_ref().map(|buffer| {
            (
                buffer.pixels.clone(),
                buffer.width as u16,
                buffer.height as u16,
            )
        })
    })
}

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
