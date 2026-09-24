//! macroquad backend for the Lynxer `game` stdlib module.
//!
//! This crate is the whole module: it exports `clynxer_module_init_v1`, every op,
//! and `clynxer_module_attach_v1`. There is no C++ shim. Macroquad owns the
//! window and event loop; once per frame it invokes the registered Lynxer
//! `update(dt)` and `draw()` callbacks through the host API.
//!
//! All coordinates are bottom-left / +Y up (Arcade convention); the draw helpers
//! convert to macroquad's top-left / +Y down space.
//!
//! Set `LYNXER_GAME_HEADLESS=1` to run without a window: `run()` executes a
//! fixed number of deterministic frames and drawing is a no-op.

mod camera;
mod draw;
mod host;
mod input;
mod sprites;
mod state;

use lynxer_abi::{export_float, export_int};
use macroquad::conf::Conf;
use macroquad::input::show_mouse;
use macroquad::time::get_fps;
use macroquad::window::{
    clear_background, next_frame, request_new_screen_size, screen_height, screen_width,
    set_fullscreen,
};
use macroquad::Window;

use lynxer_abi::LynxerHostApi;

use crate::state::{color_of, headless_requested, with, HEADLESS_DT, HEADLESS_FRAMES};

/// Runs the Lynxer update and draw callbacks for one frame. Returns false when
/// a callback failed, so the loop stops and the interpreter can rethrow.
fn run_frame(host: &'static LynxerHostApi, update: &str, draw: &str, dt: f64) -> bool {
    if !update.is_empty() && lynxer_abi::invoke(host, update, Some(dt)) != 0 {
        return false;
    }
    if !draw.is_empty() && lynxer_abi::invoke(host, draw, None) != 0 {
        return false;
    }
    true
}

export_int!(clynxer_game_init, args, {
    let title = args.string(0).to_string();
    let width = args.float(0);
    let height = args.float(1);
    with(|state| {
        state.reset();
        state.title = if title.is_empty() {
            "Lynxer".to_string()
        } else {
            title
        };
        state.width = if width > 0.0 { width } else { 800.0 };
        state.height = if height > 0.0 { height } else { 600.0 };
        state.initialized = true;
        state.headless = headless_requested();
    });
    0
});

// The title is applied when `run()` opens the window; changing it mid-run is
// not reflected because miniquad exposes no portable retitle call.
export_int!(clynxer_game_set_title, args, {
    let title = args.string(0).to_string();
    with(|state| state.title = title);
    0
});

export_int!(clynxer_game_set_background, args, {
    let color = color_of(args.int(0), args.int(1), args.int(2));
    with(|state| state.background = color);
    0
});

export_int!(clynxer_game_get_width, args, {
    let _ = args;
    with(|state| state.width as i64)
});

export_int!(clynxer_game_get_height, args, {
    let _ = args;
    with(|state| state.height as i64)
});

export_int!(clynxer_game_set_window_size, args, {
    let width = args.float(0);
    let height = args.float(1);
    with(|state| {
        state.width = width.max(1.0);
        state.height = height.max(1.0);
        if !state.headless {
            request_new_screen_size(width, height);
        }
    });
    0
});

// Miniquad has no portable runtime resize toggle, so this is accepted and
// ignored; the window is created resizable by default.
export_int!(clynxer_game_set_resizable, args, {
    let _ = args;
    0
});

export_int!(clynxer_game_set_fullscreen, args, {
    let enabled = args.int(0) != 0;
    with(|state| {
        if !state.headless {
            set_fullscreen(enabled);
        }
    });
    0
});

export_int!(clynxer_game_set_mouse_visible, args, {
    let visible = args.int(0) != 0;
    with(|state| {
        if !state.headless {
            show_mouse(visible);
        }
    });
    0
});

export_int!(clynxer_game_set_fps_cap, args, {
    with(|state| state.fps_cap = args.float(0).max(0.0));
    0
});

export_int!(clynxer_game_get_fps, args, {
    let _ = args;
    if with(|state| state.headless) {
        return 0;
    }
    get_fps() as i64
});

export_int!(clynxer_game_close, args, {
    let _ = args;
    with(|state| state.quit_requested = true);
    0
});

export_float!(clynxer_game_delta_time, args, {
    let _ = args;
    with(|state| state.dt)
});

export_float!(clynxer_game_get_time, args, {
    let _ = args;
    with(|state| state.sim_time)
});

export_int!(clynxer_game_is_open, args, {
    let _ = args;
    with(|state| (state.initialized && !state.quit_requested) as i64)
});

export_int!(clynxer_game_set_draw_callback, args, {
    let name = args.string(0).to_string();
    with(|state| state.draw_callback = name);
    0
});

export_int!(clynxer_game_set_update_callback, args, {
    let name = args.string(0).to_string();
    with(|state| state.update_callback = name);
    0
});

export_int!(clynxer_game_run, args, {
    let _ = args;
    let host = match host::host() {
        Some(host) => host,
        None => return -1,
    };
    let (headless, title, width, height, update, draw) = with(|state| {
        (
            state.headless,
            state.title.clone(),
            state.width,
            state.height,
            state.update_callback.clone(),
            state.draw_callback.clone(),
        )
    });

    if headless {
        with(|state| state.running = true);
        for _ in 0..HEADLESS_FRAMES {
            with(|state| {
                state.dt = HEADLESS_DT;
                state.sim_time += HEADLESS_DT;
            });
            let keep_going = run_frame(host, &update, &draw, HEADLESS_DT);
            if !keep_going || lynxer_abi::interrupted(host) || with(|state| state.quit_requested) {
                break;
            }
        }
        with(|state| state.running = false);
        return 0;
    }

    let configuration = Conf {
        miniquad_conf: macroquad::miniquad::conf::Conf {
            window_title: title,
            window_width: width as i32,
            window_height: height as i32,
            ..Default::default()
        },
        ..Default::default()
    };

    Window::from_config(configuration, async move {
        with(|state| state.running = true);
        loop {
            let dt = macroquad::time::get_frame_time() as f64;
            let (scroll_x, scroll_y) = macroquad::input::mouse_wheel();
            with(|state| {
                state.dt = dt;
                state.sim_time += dt;
                state.width = screen_width();
                state.height = screen_height();
                state.scroll_x = scroll_x;
                state.scroll_y = scroll_y;
            });
            clear_background(with(|state| state.background));
            let keep_going = run_frame(host, &update, &draw, dt);
            if !keep_going || lynxer_abi::interrupted(host) || with(|state| state.quit_requested) {
                break;
            }
            let cap = with(|state| state.fps_cap);
            if cap > 0.0 {
                std::thread::sleep(std::time::Duration::from_secs_f64(1.0 / cap as f64));
            }
            next_frame().await;
        }
        with(|state| state.running = false);
    });

    0
});

const OPS: &[(&str, &str, &str)] = &[
    // Window and lifecycle.
    ("init", "clynxer_game_init", "cdecl:int64(...)"),
    ("setTitle", "clynxer_game_set_title", "cdecl:int64(...)"),
    (
        "setBackground",
        "clynxer_game_set_background",
        "cdecl:int64(...)",
    ),
    ("getWidth", "clynxer_game_get_width", "cdecl:int64(...)"),
    ("getHeight", "clynxer_game_get_height", "cdecl:int64(...)"),
    (
        "setWindowSize",
        "clynxer_game_set_window_size",
        "cdecl:int64(...)",
    ),
    (
        "setResizable",
        "clynxer_game_set_resizable",
        "cdecl:int64(...)",
    ),
    (
        "setFullscreen",
        "clynxer_game_set_fullscreen",
        "cdecl:int64(...)",
    ),
    (
        "setMouseVisible",
        "clynxer_game_set_mouse_visible",
        "cdecl:int64(...)",
    ),
    ("setFPSCap", "clynxer_game_set_fps_cap", "cdecl:int64(...)"),
    ("getFPS", "clynxer_game_get_fps", "cdecl:int64(...)"),
    ("close", "clynxer_game_close", "cdecl:int64(...)"),
    ("isOpen", "clynxer_game_is_open", "cdecl:int64(...)"),
    ("deltaTime", "clynxer_game_delta_time", "cdecl:float64(...)"),
    ("getTime", "clynxer_game_get_time", "cdecl:float64(...)"),
    (
        "setDrawCallback",
        "clynxer_game_set_draw_callback",
        "cdecl:int64(...)",
    ),
    (
        "setUpdateCallback",
        "clynxer_game_set_update_callback",
        "cdecl:int64(...)",
    ),
    ("run", "clynxer_game_run", "cdecl:int64(...)"),
    // Draw loop and shapes.
    ("beginDraw", "clynxer_game_begin_draw", "cdecl:int64(...)"),
    ("endDraw", "clynxer_game_end_draw", "cdecl:int64(...)"),
    ("drawRect", "clynxer_game_draw_rect", "cdecl:int64(...)"),
    (
        "drawRectOutline",
        "clynxer_game_draw_rect_outline",
        "cdecl:int64(...)",
    ),
    ("drawCircle", "clynxer_game_draw_circle", "cdecl:int64(...)"),
    (
        "drawCircleOutline",
        "clynxer_game_draw_circle_outline",
        "cdecl:int64(...)",
    ),
    (
        "drawEllipse",
        "clynxer_game_draw_ellipse",
        "cdecl:int64(...)",
    ),
    (
        "drawEllipseOutline",
        "clynxer_game_draw_ellipse_outline",
        "cdecl:int64(...)",
    ),
    ("drawLine", "clynxer_game_draw_line", "cdecl:int64(...)"),
    (
        "drawTriangle",
        "clynxer_game_draw_triangle",
        "cdecl:int64(...)",
    ),
    (
        "drawTriangleOutline",
        "clynxer_game_draw_triangle_outline",
        "cdecl:int64(...)",
    ),
    ("drawPoint", "clynxer_game_draw_point", "cdecl:int64(...)"),
    (
        "drawRectRoundedFilled",
        "clynxer_game_draw_rect_rounded_filled",
        "cdecl:int64(...)",
    ),
    (
        "drawRectRoundedOutline",
        "clynxer_game_draw_rect_rounded_outline",
        "cdecl:int64(...)",
    ),
    ("drawStar", "clynxer_game_draw_star", "cdecl:int64(...)"),
    (
        "drawDashedLine",
        "clynxer_game_draw_dashed_line",
        "cdecl:int64(...)",
    ),
    ("drawCross", "clynxer_game_draw_cross", "cdecl:int64(...)"),
    (
        "drawGradientRect",
        "clynxer_game_draw_gradient_rect",
        "cdecl:int64(...)",
    ),
    ("drawArc", "clynxer_game_draw_arc", "cdecl:int64(...)"),
    (
        "drawArcFilled",
        "clynxer_game_draw_arc_filled",
        "cdecl:int64(...)",
    ),
    (
        "drawPolygon",
        "clynxer_game_draw_polygon",
        "cdecl:int64(...)",
    ),
    (
        "drawPolygonOutline",
        "clynxer_game_draw_polygon_outline",
        "cdecl:int64(...)",
    ),
    (
        "drawPolyline",
        "clynxer_game_draw_polyline",
        "cdecl:int64(...)",
    ),
    ("drawPoints", "clynxer_game_draw_points", "cdecl:int64(...)"),
    ("drawLines", "clynxer_game_draw_lines", "cdecl:int64(...)"),
    ("drawText", "clynxer_game_draw_text", "cdecl:int64(...)"),
    (
        "drawTextStyled",
        "clynxer_game_draw_text_styled",
        "cdecl:int64(...)",
    ),
    (
        "drawTextAnchored",
        "clynxer_game_draw_text_anchored",
        "cdecl:int64(...)",
    ),
    // Input.
    ("keyDown", "clynxer_game_key_down", "cdecl:int64(...)"),
    ("keyUp", "clynxer_game_key_up", "cdecl:int64(...)"),
    ("keyPressed", "clynxer_game_key_pressed", "cdecl:int64(...)"),
    (
        "keyReleased",
        "clynxer_game_key_released",
        "cdecl:int64(...)",
    ),
    ("keyCode", "clynxer_game_key_code", "cdecl:int64(...)"),
    ("mouseX", "clynxer_game_mouse_x", "cdecl:float64(...)"),
    ("mouseY", "clynxer_game_mouse_y", "cdecl:float64(...)"),
    (
        "mouseDeltaX",
        "clynxer_game_mouse_delta_x",
        "cdecl:float64(...)",
    ),
    (
        "mouseDeltaY",
        "clynxer_game_mouse_delta_y",
        "cdecl:float64(...)",
    ),
    (
        "mouseScrollX",
        "clynxer_game_mouse_scroll_x",
        "cdecl:float64(...)",
    ),
    (
        "mouseScrollY",
        "clynxer_game_mouse_scroll_y",
        "cdecl:float64(...)",
    ),
    ("mouseLeft", "clynxer_game_mouse_left", "cdecl:int64(...)"),
    ("mouseRight", "clynxer_game_mouse_right", "cdecl:int64(...)"),
    (
        "mouseMiddle",
        "clynxer_game_mouse_middle",
        "cdecl:int64(...)",
    ),
    (
        "mouseButtonDown",
        "clynxer_game_mouse_button_down",
        "cdecl:int64(...)",
    ),
    (
        "mouseButtonPressed",
        "clynxer_game_mouse_button_pressed",
        "cdecl:int64(...)",
    ),
    (
        "mouseButtonReleased",
        "clynxer_game_mouse_button_released",
        "cdecl:int64(...)",
    ),
    (
        "mouseButtonCode",
        "clynxer_game_mouse_button_code",
        "cdecl:int64(...)",
    ),
    // Sprites.
    (
        "makeSolidSprite",
        "clynxer_game_make_solid_sprite",
        "cdecl:int64(...)",
    ),
    ("loadSprite", "clynxer_game_load_sprite", "cdecl:int64(...)"),
    (
        "loadTexture",
        "clynxer_game_load_texture",
        "cdecl:int64(...)",
    ),
    (
        "setSpriteTexture",
        "clynxer_game_set_sprite_texture",
        "cdecl:int64(...)",
    ),
    (
        "getSpriteX",
        "clynxer_game_get_sprite_x",
        "cdecl:float64(...)",
    ),
    (
        "getSpriteY",
        "clynxer_game_get_sprite_y",
        "cdecl:float64(...)",
    ),
    (
        "getSpriteAngle",
        "clynxer_game_get_sprite_angle",
        "cdecl:float64(...)",
    ),
    (
        "getSpriteScale",
        "clynxer_game_get_sprite_scale",
        "cdecl:float64(...)",
    ),
    (
        "getSpriteWidth",
        "clynxer_game_get_sprite_width",
        "cdecl:float64(...)",
    ),
    (
        "getSpriteHeight",
        "clynxer_game_get_sprite_height",
        "cdecl:float64(...)",
    ),
    (
        "getSpriteVX",
        "clynxer_game_get_sprite_vx",
        "cdecl:float64(...)",
    ),
    (
        "getSpriteVY",
        "clynxer_game_get_sprite_vy",
        "cdecl:float64(...)",
    ),
    (
        "getSpriteAngularVelocity",
        "clynxer_game_get_sprite_angular_velocity",
        "cdecl:float64(...)",
    ),
    (
        "getSpriteAlpha",
        "clynxer_game_get_sprite_alpha",
        "cdecl:int64(...)",
    ),
    (
        "getSpriteVisible",
        "clynxer_game_get_sprite_visible",
        "cdecl:int64(...)",
    ),
    (
        "getSpritePosition",
        "clynxer_game_get_sprite_position",
        "cdecl:cstring(...)",
    ),
    (
        "setSpritePos",
        "clynxer_game_set_sprite_pos",
        "cdecl:int64(...)",
    ),
    (
        "setSpritePosition",
        "clynxer_game_set_sprite_pos",
        "cdecl:int64(...)",
    ),
    (
        "setSpriteAngle",
        "clynxer_game_set_sprite_angle",
        "cdecl:int64(...)",
    ),
    (
        "setSpriteScale",
        "clynxer_game_set_sprite_scale",
        "cdecl:int64(...)",
    ),
    (
        "setSpriteVelocity",
        "clynxer_game_set_sprite_velocity",
        "cdecl:int64(...)",
    ),
    (
        "setSpriteAngularVelocity",
        "clynxer_game_set_sprite_angular_velocity",
        "cdecl:int64(...)",
    ),
    ("stopSprite", "clynxer_game_stop_sprite", "cdecl:int64(...)"),
    (
        "moveSpriteToward",
        "clynxer_game_move_sprite_toward",
        "cdecl:int64(...)",
    ),
    (
        "faceSpriteTo",
        "clynxer_game_face_sprite_to",
        "cdecl:int64(...)",
    ),
    (
        "setSpriteAlpha",
        "clynxer_game_set_sprite_alpha",
        "cdecl:int64(...)",
    ),
    (
        "setSpriteColor",
        "clynxer_game_set_sprite_color",
        "cdecl:int64(...)",
    ),
    (
        "setSpriteVisible",
        "clynxer_game_set_sprite_visible",
        "cdecl:int64(...)",
    ),
    (
        "flipSpriteH",
        "clynxer_game_flip_sprite_h",
        "cdecl:int64(...)",
    ),
    (
        "flipSpriteV",
        "clynxer_game_flip_sprite_v",
        "cdecl:int64(...)",
    ),
    (
        "destroySprite",
        "clynxer_game_destroy_sprite",
        "cdecl:int64(...)",
    ),
    (
        "spriteExists",
        "clynxer_game_sprite_exists",
        "cdecl:int64(...)",
    ),
    (
        "updateSprite",
        "clynxer_game_update_sprite",
        "cdecl:int64(...)",
    ),
    ("drawSprite", "clynxer_game_draw_sprite", "cdecl:int64(...)"),
    (
        "drawTexture",
        "clynxer_game_draw_texture",
        "cdecl:int64(...)",
    ),
    (
        "drawTextureAt",
        "clynxer_game_draw_texture_at",
        "cdecl:int64(...)",
    ),
    (
        "drawTextureRect",
        "clynxer_game_draw_texture_rect",
        "cdecl:int64(...)",
    ),
    (
        "spriteCollides",
        "clynxer_game_sprite_collides",
        "cdecl:int64(...)",
    ),
    (
        "spriteCollidesWithList",
        "clynxer_game_sprite_collides_with_list",
        "cdecl:int64(...)",
    ),
    (
        "getCollidingSprites",
        "clynxer_game_get_colliding_sprites",
        "cdecl:cstring(...)",
    ),
    (
        "spriteDistance",
        "clynxer_game_sprite_distance",
        "cdecl:float64(...)",
    ),
    ("spriteNear", "clynxer_game_sprite_near", "cdecl:int64(...)"),
    // Sprite lists.
    (
        "makeSpriteList",
        "clynxer_game_make_sprite_list",
        "cdecl:int64(...)",
    ),
    ("addToList", "clynxer_game_add_to_list", "cdecl:int64(...)"),
    (
        "removeSpriteFromList",
        "clynxer_game_remove_sprite_from_list",
        "cdecl:int64(...)",
    ),
    (
        "clearSpriteList",
        "clynxer_game_clear_sprite_list",
        "cdecl:int64(...)",
    ),
    (
        "getSpriteListCount",
        "clynxer_game_get_sprite_list_count",
        "cdecl:int64(...)",
    ),
    (
        "drawSpriteList",
        "clynxer_game_draw_sprite_list",
        "cdecl:int64(...)",
    ),
    (
        "updateSpriteList",
        "clynxer_game_update_sprite_list",
        "cdecl:int64(...)",
    ),
    // Camera.
    ("makeCamera", "clynxer_game_make_camera", "cdecl:int64(...)"),
    ("useCamera", "clynxer_game_use_camera", "cdecl:int64(...)"),
    (
        "setCameraPos",
        "clynxer_game_set_camera_pos",
        "cdecl:int64(...)",
    ),
    (
        "getCameraX",
        "clynxer_game_get_camera_x",
        "cdecl:float64(...)",
    ),
    (
        "getCameraY",
        "clynxer_game_get_camera_y",
        "cdecl:float64(...)",
    ),
    ("zoomCamera", "clynxer_game_zoom_camera", "cdecl:int64(...)"),
    (
        "getCameraZoom",
        "clynxer_game_get_camera_zoom",
        "cdecl:float64(...)",
    ),
    (
        "smoothScrollCamera",
        "clynxer_game_smooth_scroll_camera",
        "cdecl:int64(...)",
    ),
    (
        "resetCamera",
        "clynxer_game_reset_camera",
        "cdecl:int64(...)",
    ),
    // Grid helpers.
    (
        "screenToTile",
        "clynxer_game_screen_to_tile",
        "cdecl:cstring(...)",
    ),
    (
        "tileToScreen",
        "clynxer_game_tile_to_screen",
        "cdecl:cstring(...)",
    ),
];

lynxer_abi::clynxer_module!(OPS);
