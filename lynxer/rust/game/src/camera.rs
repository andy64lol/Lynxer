//! Camera ops. Cameras are a transform applied by the draw ops, not macroquad
//! `Camera2D`, which keeps world coordinates bottom-left / +Y up and makes the
//! behaviour testable headlessly.

use clynxer_abi::{export_float, export_int};
use crate::state::{with, Camera};

export_int!(lynxer_game_make_camera, args, {
    let _ = args;
    with(|state| {
        state.cameras.push(Camera::new());
        (state.cameras.len() - 1) as i64
    })
});

export_int!(lynxer_game_use_camera, args, {
    let index = args.int(0);
    with(|state| {
        if index >= 0 && (index as usize) < state.cameras.len() {
            state.active_camera = Some(index as usize);
        }
        0
    })
});

export_int!(lynxer_game_set_camera_pos, args, {
    let index = args.int(0);
    let x = args.float(1);
    let y = args.float(2);
    with(|state| {
        if let Some(camera) = state.cameras.get_mut(index.max(0) as usize) {
            camera.x = x;
            camera.y = y;
        }
        0
    })
});

export_float!(lynxer_game_get_camera_x, args, {
    let index = args.int(0);
    with(|state| {
        state
            .cameras
            .get(index.max(0) as usize)
            .map(|camera| camera.x as f64)
            .unwrap_or(0.0)
    })
});

export_float!(lynxer_game_get_camera_y, args, {
    let index = args.int(0);
    with(|state| {
        state
            .cameras
            .get(index.max(0) as usize)
            .map(|camera| camera.y as f64)
            .unwrap_or(0.0)
    })
});

export_int!(lynxer_game_zoom_camera, args, {
    let index = args.int(0);
    let zoom = args.float(1);
    with(|state| {
        if let Some(camera) = state.cameras.get_mut(index.max(0) as usize) {
            camera.zoom = if zoom > 0.0 { zoom } else { 1.0 };
        }
        0
    })
});

export_float!(lynxer_game_get_camera_zoom, args, {
    let index = args.int(0);
    with(|state| {
        state
            .cameras
            .get(index.max(0) as usize)
            .map(|camera| camera.zoom as f64)
            .unwrap_or(1.0)
    })
});

export_int!(lynxer_game_smooth_scroll_camera, args, {
    let index = args.int(0);
    let target_x = args.float(1);
    let target_y = args.float(2);
    let speed = args.float(3);
    with(|state| {
        if let Some(camera) = state.cameras.get_mut(index.max(0) as usize) {
            camera.x += (target_x - camera.x) * speed;
            camera.y += (target_y - camera.y) * speed;
        }
        0
    })
});

export_int!(lynxer_game_reset_camera, args, {
    let _ = args;
    with(|state| {
        state.active_camera = None;
        0
    })
});
