//! Textures, images and render targets.
//!
//! Loads are synchronous: the file is read with `std::fs` and decoded by
//! macroquad's `*_from_bytes`/`from_file_with_format` constructors, so no async
//! plumbing is needed. Handles index into `State`'s registries; `-1` means the
//! load failed or the module is headless.

use macroquad::camera::{set_camera, set_default_camera, Camera2D};
use macroquad::color::Color;
use macroquad::math::{vec2, Rect};
use macroquad::texture::{
    build_textures_atlas, draw_texture, draw_texture_ex, get_screen_data, render_target,
    render_target_msaa, DrawTextureParams, FilterMode, Image, RenderTarget, Texture2D,
};

use crate::state::{color_of_a, number, with};

fn headless() -> bool {
    with(|state| state.headless)
}

fn push_image(image: Image) -> i64 {
    with(|state| {
        state.images.push(Some(image));
        (state.images.len() - 1) as i64
    })
}

fn push_texture(texture: Texture2D) -> i64 {
    with(|state| {
        state.textures.push(Some(texture));
        (state.textures.len() - 1) as i64
    })
}

fn push_target(target: RenderTarget) -> i64 {
    with(|state| {
        state.targets.push(Some(target));
        (state.targets.len() - 1) as i64
    })
}

pub fn load_texture(path: &str) -> i64 {
    if headless() || path.is_empty() {
        return -1;
    }
    match std::fs::read(path) {
        Ok(bytes) => push_texture(Texture2D::from_file_with_format(&bytes, None)),
        Err(_) => -1,
    }
}

pub fn load_image(path: &str) -> i64 {
    if path.is_empty() {
        return -1;
    }
    match std::fs::read(path) {
        Ok(bytes) => match Image::from_file_with_format(&bytes, None) {
            Ok(image) => push_image(image),
            Err(_) => -1,
        },
        Err(_) => -1,
    }
}

pub fn texture_from_image(image_handle: i64) -> i64 {
    if headless() {
        return -1;
    }
    match with(|state| state.image(image_handle).cloned()) {
        Some(image) => push_texture(Texture2D::from_image(&image)),
        None => -1,
    }
}

pub fn image_from_texture(texture_handle: i64) -> i64 {
    match with(|state| state.texture(texture_handle).cloned()) {
        Some(texture) => push_image(texture.get_texture_data()),
        None => -1,
    }
}

pub fn gen_image(width: i64, height: i64, r: i64, g: i64, b: i64, a: i64) -> i64 {
    let color = color_of_a(r, g, b, a);
    push_image(Image::gen_image_color(
        width.clamp(1, u16::MAX as i64) as u16,
        height.clamp(1, u16::MAX as i64) as u16,
        color,
    ))
}

pub fn image_width(image_handle: i64) -> i64 {
    with(|state| {
        state
            .image(image_handle)
            .map(|i| i.width as i64)
            .unwrap_or(-1)
    })
}

pub fn image_height(image_handle: i64) -> i64 {
    with(|state| {
        state
            .image(image_handle)
            .map(|i| i.height as i64)
            .unwrap_or(-1)
    })
}

pub fn image_get_pixel(image_handle: i64, x: i64, y: i64) -> String {
    match with(|state| {
        state
            .image(image_handle)
            .map(|image| image.get_pixel(x.max(0) as u32, y.max(0) as u32))
    }) {
        Some(color) => format!(
            "[{},{},{},{}]",
            (color.r * 255.0).round() as i64,
            (color.g * 255.0).round() as i64,
            (color.b * 255.0).round() as i64,
            (color.a * 255.0).round() as i64
        ),
        None => "[]".to_string(),
    }
}

pub fn image_set_pixel(image_handle: i64, x: i64, y: i64, r: i64, g: i64, b: i64, a: i64) -> i64 {
    let color = color_of_a(r, g, b, a);
    with(|state| match state.image_mut(image_handle) {
        Some(image) => {
            image.set_pixel(x.max(0) as u32, y.max(0) as u32, color);
            0
        }
        None => -1,
    })
}

pub fn export_image(image_handle: i64, path: &str) -> i64 {
    if path.is_empty() {
        return -1;
    }
    with(|state| match state.image(image_handle) {
        Some(image) => {
            image.export_png(path);
            0
        }
        None => -1,
    })
}

pub fn texture_width(texture_handle: i64) -> f32 {
    with(|state| {
        state
            .texture(texture_handle)
            .map(|t| t.width())
            .unwrap_or(0.0)
    })
}

pub fn texture_height(texture_handle: i64) -> f32 {
    with(|state| {
        state
            .texture(texture_handle)
            .map(|t| t.height())
            .unwrap_or(0.0)
    })
}

pub fn texture_size(texture_handle: i64) -> String {
    with(|state| match state.texture(texture_handle) {
        Some(texture) => format!("[{},{}]", number(texture.width()), number(texture.height())),
        None => "[0,0]".to_string(),
    })
}

pub fn draw(texture_handle: i64, x: f32, y: f32, color: Color) {
    let texture = match with(|state| state.texture(texture_handle).cloned()) {
        Some(texture) => texture,
        None => return,
    };
    draw_texture(&texture, x, y, color);
}

pub fn draw_scaled(texture_handle: i64, x: f32, y: f32, w: f32, h: f32, color: Color) {
    let texture = match with(|state| state.texture(texture_handle).cloned()) {
        Some(texture) => texture,
        None => return,
    };
    draw_texture_ex(
        &texture,
        x,
        y,
        color,
        DrawTextureParams {
            dest_size: Some(vec2(w, h)),
            ..Default::default()
        },
    );
}

#[allow(clippy::too_many_arguments)]
pub fn draw_region(
    texture_handle: i64,
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    source_x: f32,
    source_y: f32,
    source_w: f32,
    source_h: f32,
    color: Color,
) {
    let texture = match with(|state| state.texture(texture_handle).cloned()) {
        Some(texture) => texture,
        None => return,
    };
    draw_texture_ex(
        &texture,
        x,
        y,
        color,
        DrawTextureParams {
            dest_size: Some(vec2(w, h)),
            source: Some(Rect::new(source_x, source_y, source_w, source_h)),
            ..Default::default()
        },
    );
}

pub fn draw_rotated(
    texture_handle: i64,
    x: f32,
    y: f32,
    w: f32,
    h: f32,
    rotation: f32,
    color: Color,
) {
    let texture = match with(|state| state.texture(texture_handle).cloned()) {
        Some(texture) => texture,
        None => return,
    };
    draw_texture_ex(
        &texture,
        x,
        y,
        color,
        DrawTextureParams {
            dest_size: Some(vec2(w, h)),
            rotation,
            pivot: Some(vec2(x + w / 2.0, y + h / 2.0)),
            ..Default::default()
        },
    );
}

pub fn set_filter(texture_handle: i64, nearest: bool) {
    let texture = match with(|state| state.texture(texture_handle).cloned()) {
        Some(texture) => texture,
        None => return,
    };
    texture.set_filter(if nearest {
        FilterMode::Nearest
    } else {
        FilterMode::Linear
    });
}

pub fn build_atlas() {
    if !headless() {
        build_textures_atlas();
    }
}

pub fn new_render_target(width: i64, height: i64, msaa: bool) -> i64 {
    if headless() {
        return -1;
    }
    let width = width.clamp(1, u32::MAX as i64) as u32;
    let height = height.clamp(1, u32::MAX as i64) as u32;
    let target = if msaa {
        render_target_msaa(width, height)
    } else {
        render_target(width, height)
    };
    push_target(target)
}

/// Registers a render target's texture so it can be drawn like any other.
pub fn render_target_texture(target_handle: i64) -> i64 {
    match with(|state| state.target(target_handle).map(|t| t.texture.clone())) {
        Some(texture) => push_texture(texture),
        None => -1,
    }
}

/// Redirects drawing into a render target (`-1` if the handle is unknown).
pub fn set_render_target(target_handle: i64) -> i64 {
    let target = match with(|state| state.target(target_handle).cloned()) {
        Some(target) => target,
        None => return -1,
    };
    if headless() {
        return -1;
    }
    let mut camera = Camera2D::from_display_rect(Rect::new(
        0.0,
        0.0,
        target.texture.width(),
        target.texture.height(),
    ));
    camera.render_target = Some(target);
    set_camera(&camera);
    0
}

pub fn end_render_target() {
    if !headless() {
        set_default_camera();
    }
}

/// Registers a full-window screenshot as an image handle.
pub fn screen_image() -> i64 {
    if headless() {
        return -1;
    }
    push_image(get_screen_data())
}
