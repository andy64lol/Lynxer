//! 2D and 3D cameras.
//!
//! The last 2D camera is kept in `State` so `screenToWorld`/`worldToScreen` can
//! be answered with pure math, without touching the GL context.

use macroquad::camera::{set_camera, set_default_camera, Camera2D, Camera3D, Projection};
use macroquad::math::{vec2, vec3, Rect};

use crate::state::{number, with, CameraState};

pub fn set_2d(
    target_x: f32,
    target_y: f32,
    zoom: f32,
    rotation: f32,
    offset_x: f32,
    offset_y: f32,
) {
    with(|state| {
        state.camera2d = CameraState {
            target_x,
            target_y,
            zoom,
            rotation,
            offset_x,
            offset_y,
        };
        if state.headless {
            return;
        }
        let camera = Camera2D {
            rotation,
            zoom: vec2(zoom, zoom),
            target: vec2(target_x, target_y),
            offset: vec2(offset_x, offset_y),
            render_target: None,
            viewport: None,
        };
        set_camera(&camera);
    });
}

pub fn set_2d_rect(x: f32, y: f32, w: f32, h: f32) {
    with(|state| {
        let camera = Camera2D::from_display_rect(Rect::new(x, y, w, h));
        state.camera2d = CameraState {
            target_x: camera.target.x,
            target_y: camera.target.y,
            zoom: camera.zoom.x,
            rotation: camera.rotation,
            offset_x: camera.offset.x,
            offset_y: camera.offset.y,
        };
        if !state.headless {
            set_camera(&camera);
        }
    });
}

pub fn set_default() {
    with(|state| {
        state.camera2d = CameraState {
            target_x: 0.0,
            target_y: 0.0,
            zoom: 1.0,
            rotation: 0.0,
            offset_x: 0.0,
            offset_y: 0.0,
        };
        if !state.headless {
            set_default_camera();
        }
    });
}

pub fn push_state() {
    if !with(|state| state.headless) {
        macroquad::camera::push_camera_state();
    }
}

pub fn pop_state() {
    if !with(|state| state.headless) {
        macroquad::camera::pop_camera_state();
    }
}

/// Projection flags for [`set_3d`]: anything but `0` is orthographic.
pub fn set_3d(
    position_x: f32,
    position_y: f32,
    position_z: f32,
    target_x: f32,
    target_y: f32,
    target_z: f32,
    up_x: f32,
    up_y: f32,
    up_z: f32,
    fovy: f32,
    orthographic: bool,
    z_near: f32,
    z_far: f32,
) {
    with(|state| {
        if state.headless {
            return;
        }
        let camera = Camera3D {
            position: vec3(position_x, position_y, position_z),
            target: vec3(target_x, target_y, target_z),
            up: vec3(up_x, up_y, up_z),
            fovy,
            aspect: None,
            projection: if orthographic {
                Projection::Orthographics
            } else {
                Projection::Perspective
            },
            render_target: None,
            viewport: None,
            z_near,
            z_far,
        };
        set_camera(&camera);
    });
}

/// The current 2D camera as `[targetX,targetY,zoom,rotation,offsetX,offsetY]`.
pub fn target() -> String {
    with(|state| {
        let camera = state.camera2d;
        format!(
            "[{},{},{},{},{},{}]",
            number(camera.target_x),
            number(camera.target_y),
            number(camera.zoom),
            number(camera.rotation),
            number(camera.offset_x),
            number(camera.offset_y)
        )
    })
}

/// macroquad projects through the live window, so this needs a GL context; in
/// headless mode it returns `[0,0]`.
pub fn screen_to_world(x: f32, y: f32) -> String {
    with(|state| {
        if state.headless {
            return "[0,0]".to_string();
        }
        let saved = state.camera2d;
        let camera = Camera2D {
            rotation: saved.rotation,
            zoom: vec2(saved.zoom, saved.zoom),
            target: vec2(saved.target_x, saved.target_y),
            offset: vec2(saved.offset_x, saved.offset_y),
            render_target: None,
            viewport: None,
        };
        let point = camera.screen_to_world(vec2(x, y));
        format!("[{},{}]", number(point.x), number(point.y))
    })
}

/// See [`screen_to_world`] for the headless placeholder.
pub fn world_to_screen(x: f32, y: f32) -> String {
    with(|state| {
        if state.headless {
            return "[0,0]".to_string();
        }
        let saved = state.camera2d;
        let camera = Camera2D {
            rotation: saved.rotation,
            zoom: vec2(saved.zoom, saved.zoom),
            target: vec2(saved.target_x, saved.target_y),
            offset: vec2(saved.offset_x, saved.offset_y),
            render_target: None,
            viewport: None,
        };
        let point = camera.world_to_screen(vec2(x, y));
        format!("[{},{}]", number(point.x), number(point.y))
    })
}
