//! 2D shape primitives.
//!
//! Colours arrive as macroquad `Color` values built by the export shims, so the
//! ops here only deal with geometry. Every op is a no-op in headless mode.
//!
//! Rotation units follow macroquad: radians for the rotated-rectangle ops,
//! degrees for polygons, ellipses and arcs.

use macroquad::color::Color;
use macroquad::math::vec2;
use macroquad::shapes::{
    draw_arc, draw_circle, draw_circle_lines, draw_ellipse, draw_ellipse_lines, draw_hexagon,
    draw_line, draw_poly, draw_poly_lines, draw_rectangle, draw_rectangle_ex, draw_rectangle_lines,
    draw_rectangle_lines_ex, draw_triangle, draw_triangle_lines, DrawRectangleParams,
};

use crate::state::with;

fn headless() -> bool {
    with(|state| state.headless)
}

pub fn clear_background(color: Color) {
    if headless() {
        return;
    }
    macroquad::window::clear_background(color);
}

pub fn line(x1: f32, y1: f32, x2: f32, y2: f32, thickness: f32, color: Color) {
    if headless() {
        return;
    }
    draw_line(x1, y1, x2, y2, thickness, color);
}

pub fn triangle(x1: f32, y1: f32, x2: f32, y2: f32, x3: f32, y3: f32, color: Color) {
    if headless() {
        return;
    }
    draw_triangle(vec2(x1, y1), vec2(x2, y2), vec2(x3, y3), color);
}

pub fn triangle_lines(
    x1: f32,
    y1: f32,
    x2: f32,
    y2: f32,
    x3: f32,
    y3: f32,
    thickness: f32,
    color: Color,
) {
    if headless() {
        return;
    }
    draw_triangle_lines(vec2(x1, y1), vec2(x2, y2), vec2(x3, y3), thickness, color);
}

pub fn rectangle(x: f32, y: f32, w: f32, h: f32, color: Color) {
    if headless() {
        return;
    }
    draw_rectangle(x, y, w, h, color);
}

pub fn rectangle_lines(x: f32, y: f32, w: f32, h: f32, thickness: f32, color: Color) {
    if headless() {
        return;
    }
    draw_rectangle_lines(x, y, w, h, thickness, color);
}

/// `origin_x`/`origin_y` is the rotation pivot as a fraction of the rectangle
/// (`0,0` = top-left, `0.5,0.5` = centre). `rotation` is in radians.
pub fn rectangle_rotated(
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    rotation: f32,
    origin_x: f32,
    origin_y: f32,
    color: Color,
) {
    if headless() {
        return;
    }
    draw_rectangle_ex(
        x,
        y,
        w,
        h,
        DrawRectangleParams {
            offset: vec2(origin_x, origin_y),
            rotation,
            color,
        },
    );
}

pub fn rectangle_lines_rotated(
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
    if headless() {
        return;
    }
    draw_rectangle_lines_ex(
        x,
        y,
        w,
        h,
        thickness,
        DrawRectangleParams {
            offset: vec2(origin_x, origin_y),
            rotation,
            color,
        },
    );
}

pub fn poly(x: f32, y: f32, sides: i64, radius: f32, rotation: f32, color: Color) {
    if headless() {
        return;
    }
    draw_poly(x, y, sides.clamp(3, 255) as u8, radius, rotation, color);
}

pub fn poly_lines(
    x: f32,
    y: f32,
    sides: i64,
    radius: f32,
    rotation: f32,
    thickness: f32,
    color: Color,
) {
    if headless() {
        return;
    }
    draw_poly_lines(
        x,
        y,
        sides.clamp(3, 255) as u8,
        radius,
        rotation,
        thickness,
        color,
    );
}

pub fn circle(x: f32, y: f32, radius: f32, color: Color) {
    if headless() {
        return;
    }
    draw_circle(x, y, radius, color);
}

pub fn circle_lines(x: f32, y: f32, radius: f32, thickness: f32, color: Color) {
    if headless() {
        return;
    }
    draw_circle_lines(x, y, radius, thickness, color);
}

pub fn ellipse(x: f32, y: f32, w: f32, h: f32, rotation: f32, color: Color) {
    if headless() {
        return;
    }
    draw_ellipse(x, y, w, h, rotation, color);
}

pub fn ellipse_lines(x: f32, y: f32, w: f32, h: f32, rotation: f32, thickness: f32, color: Color) {
    if headless() {
        return;
    }
    draw_ellipse_lines(x, y, w, h, rotation, thickness, color);
}

pub fn arc(
    x: f32,
    y: f32,
    sides: i64,
    radius: f32,
    rotation: f32,
    thickness: f32,
    arc: f32,
    color: Color,
) {
    if headless() {
        return;
    }
    draw_arc(
        x,
        y,
        sides.clamp(3, 255) as u8,
        radius,
        rotation,
        thickness,
        arc,
        color,
    );
}

pub fn hexagon(x: f32, y: f32, size: f32, border: f32, vertical: bool, color: Color) {
    if headless() {
        return;
    }
    draw_hexagon(x, y, size, border, vertical, color, color);
}
