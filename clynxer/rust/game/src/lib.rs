//! macroquad backend for the Clynxer `game` stdlib module.
//!
//! `game.cpp` links this crate as a static library, registers the exported ops
//! with the interpreter, and drives `lynxer_game_run`. Macroquad owns the
//! window and event loop; once per frame it calls back into `game.cpp`, which
//! invokes the registered Lynxer `update`/`draw` callbacks.
//!
//! All coordinates are bottom-left / +Y up (Arcade convention); the draw helpers
//! convert to macroquad's top-left / +Y down space.
//!
//! Set `CLYNXER_GAME_HEADLESS=1` to run without a window: `run()` executes a
//! fixed number of deterministic frames and drawing is a no-op.

#[macro_use]
mod ffi;

mod camera;
mod draw;
mod input;
mod sprites;
mod state;

use core::ffi::c_void;

use macroquad::conf::Conf;
use macroquad::input::show_mouse;
use macroquad::time::get_fps;
use macroquad::window::{
    clear_background, next_frame, request_new_screen_size, screen_height, screen_width,
    set_fullscreen,
};
use macroquad::Window;

use crate::state::{color_of, headless_requested, with, HEADLESS_DT, HEADLESS_FRAMES};

game_export_int!(lynxer_game_init, args, {
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
game_export_int!(lynxer_game_set_title, args, {
    let title = args.string(0).to_string();
    with(|state| state.title = title);
    0
});

game_export_int!(lynxer_game_set_background, args, {
    let color = color_of(args.int(0), args.int(1), args.int(2));
    with(|state| state.background = color);
    0
});

game_export_int!(lynxer_game_get_width, args, {
    let _ = args;
    with(|state| state.width as i64)
});

game_export_int!(lynxer_game_get_height, args, {
    let _ = args;
    with(|state| state.height as i64)
});

game_export_int!(lynxer_game_set_window_size, args, {
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
game_export_int!(lynxer_game_set_resizable, args, {
    let _ = args;
    0
});

game_export_int!(lynxer_game_set_fullscreen, args, {
    let enabled = args.int(0) != 0;
    with(|state| {
        if !state.headless {
            set_fullscreen(enabled);
        }
    });
    0
});

game_export_int!(lynxer_game_set_mouse_visible, args, {
    let visible = args.int(0) != 0;
    with(|state| {
        if !state.headless {
            show_mouse(visible);
        }
    });
    0
});

game_export_int!(lynxer_game_set_fps_cap, args, {
    with(|state| state.fps_cap = args.float(0).max(0.0));
    0
});

game_export_int!(lynxer_game_get_fps, args, {
    let _ = args;
    if with(|state| state.headless) {
        return 0;
    }
    get_fps() as i64
});

game_export_int!(lynxer_game_close, args, {
    let _ = args;
    with(|state| state.quit_requested = true);
    0
});

game_export_float!(lynxer_game_delta_time, args, {
    let _ = args;
    with(|state| state.dt)
});

game_export_float!(lynxer_game_get_time, args, {
    let _ = args;
    with(|state| state.sim_time)
});

game_export_int!(lynxer_game_is_open, args, {
    let _ = args;
    with(|state| (state.initialized && !state.quit_requested) as i64)
});

/// Runs the frame loop. `frame` is called once per frame with the elapsed time
/// in seconds; `quit` returns non-zero when the interpreter wants to stop
/// (for example because Ctrl-C was received).
#[no_mangle]
pub extern "C" fn lynxer_game_run(
    frame: extern "C" fn(*mut c_void, f64),
    user: *mut c_void,
    quit: extern "C" fn() -> i32,
) -> i32 {
    let (headless, title, width, height) = with(|state| {
        (
            state.headless,
            state.title.clone(),
            state.width,
            state.height,
        )
    });

    if headless {
        with(|state| state.running = true);
        for _ in 0..HEADLESS_FRAMES {
            with(|state| {
                state.dt = HEADLESS_DT;
                state.sim_time += HEADLESS_DT;
            });
            frame(user, HEADLESS_DT);
            if quit() != 0 || with(|state| state.quit_requested) {
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
            frame(user, dt);
            if quit() != 0 || with(|state| state.quit_requested) {
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
}
