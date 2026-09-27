//! 3D primitives (macroquad's `models` module).
//!
//! The primitives are drawn untextured — texturing them would need a texture
//! handle per call and no fixture can verify it without a display.

use macroquad::color::Color;
use macroquad::math::{vec2, vec3};
use macroquad::models::{
    draw_affine_parallelepiped, draw_affine_parallelogram, draw_cube, draw_cube_wires,
    draw_cylinder, draw_cylinder_ex, draw_cylinder_wires, draw_grid, draw_line_3d, draw_plane,
    draw_sphere, draw_sphere_ex, draw_sphere_wires, DrawCylinderParams, DrawSphereParams,
};

use crate::state::with;

fn headless() -> bool {
    with(|state| state.headless)
}

pub fn line(x1: f32, y1: f32, z1: f32, x2: f32, y2: f32, z2: f32, color: Color) {
    if headless() {
        return;
    }
    draw_line_3d(vec3(x1, y1, z1), vec3(x2, y2, z2), color);
}

pub fn cube(x: f32, y: f32, z: f32, w: f32, h: f32, d: f32, color: Color) {
    if headless() {
        return;
    }
    draw_cube(vec3(x, y, z), vec3(w, h, d), None, color);
}

pub fn cube_wires(x: f32, y: f32, z: f32, w: f32, h: f32, d: f32, color: Color) {
    if headless() {
        return;
    }
    draw_cube_wires(vec3(x, y, z), vec3(w, h, d), color);
}

pub fn sphere(x: f32, y: f32, z: f32, radius: f32, color: Color) {
    if headless() {
        return;
    }
    draw_sphere(vec3(x, y, z), radius, None, color);
}

pub fn sphere_wires(x: f32, y: f32, z: f32, radius: f32, color: Color) {
    if headless() {
        return;
    }
    draw_sphere_wires(vec3(x, y, z), radius, None, color);
}

/// `rings`/`slices` control the tessellation (macroquad defaults to 16/16).
pub fn sphere_ex(x: f32, y: f32, z: f32, radius: f32, rings: i64, slices: i64, color: Color) {
    if headless() {
        return;
    }
    draw_sphere_ex(
        vec3(x, y, z),
        radius,
        None,
        color,
        DrawSphereParams {
            rings: rings.clamp(1, 512) as usize,
            slices: slices.clamp(1, 512) as usize,
            ..Default::default()
        },
    );
}

pub fn cylinder(
    x: f32,
    y: f32,
    z: f32,
    radius_top: f32,
    radius_bottom: f32,
    height: f32,
    color: Color,
) {
    if headless() {
        return;
    }
    draw_cylinder(
        vec3(x, y, z),
        radius_top,
        radius_bottom,
        height,
        None,
        color,
    );
}

pub fn cylinder_wires(
    x: f32,
    y: f32,
    z: f32,
    radius_top: f32,
    radius_bottom: f32,
    height: f32,
    color: Color,
) {
    if headless() {
        return;
    }
    draw_cylinder_wires(
        vec3(x, y, z),
        radius_top,
        radius_bottom,
        height,
        None,
        color,
    );
}

/// Straight-sided cylinder with an explicit side count.
pub fn cylinder_ex(x: f32, y: f32, z: f32, radius: f32, height: f32, sides: i64, color: Color) {
    if headless() {
        return;
    }
    draw_cylinder_ex(
        vec3(x, y, z),
        radius,
        radius,
        height,
        None,
        color,
        DrawCylinderParams {
            sides: sides.clamp(3, 512) as usize,
            ..Default::default()
        },
    );
}

pub fn plane(x: f32, y: f32, z: f32, w: f32, h: f32, color: Color) {
    if headless() {
        return;
    }
    draw_plane(vec3(x, y, z), vec2(w, h), None, color);
}

pub fn grid(slices: i64, spacing: f32, axes_color: Color, other_color: Color) {
    if headless() {
        return;
    }
    draw_grid(
        slices.clamp(1, 1024) as u32,
        spacing,
        axes_color,
        other_color,
    );
}

#[allow(clippy::too_many_arguments)]
pub fn parallelogram(
    ox: f32,
    oy: f32,
    oz: f32,
    e1x: f32,
    e1y: f32,
    e1z: f32,
    e2x: f32,
    e2y: f32,
    e2z: f32,
    color: Color,
) {
    if headless() {
        return;
    }
    draw_affine_parallelogram(
        vec3(ox, oy, oz),
        vec3(e1x, e1y, e1z),
        vec3(e2x, e2y, e2z),
        None,
        color,
    );
}

#[allow(clippy::too_many_arguments)]
pub fn parallelepiped(
    ox: f32,
    oy: f32,
    oz: f32,
    e1x: f32,
    e1y: f32,
    e1z: f32,
    e2x: f32,
    e2y: f32,
    e2z: f32,
    e3x: f32,
    e3y: f32,
    e3z: f32,
    color: Color,
) {
    if headless() {
        return;
    }
    draw_affine_parallelepiped(
        vec3(ox, oy, oz),
        vec3(e1x, e1y, e1z),
        vec3(e2x, e2y, e2z),
        vec3(e3x, e3y, e3z),
        None,
        color,
    );
}
