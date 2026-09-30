//! Window, frame-loop and time ops.
//!
//! Everything that touches the GL context is skipped in headless mode; the
//! queries then answer from the configured window instead.

use macroquad::input::{is_quit_requested, prevent_quit, set_cursor_grab, show_mouse};
use macroquad::miniquad::window::{
    clipboard_get, clipboard_set, get_window_position, request_quit, set_mouse_cursor,
};
// `CursorIcon` is a top-level miniquad enum; the `window` module only imports it
// privately, so reach it through the crate root.
use macroquad::miniquad::CursorIcon;
use macroquad::texture::{set_default_filter_mode as set_filter_mode, FilterMode};
use macroquad::time::{get_fps, get_frame_time, get_time};
use macroquad::window::{request_new_screen_size, screen_dpi_scale};

use crate::state::{color_of_a, with};

pub fn init(title: &str, width: f32, height: f32) {
    with(|state| {
        state.reset();
        state.title = if title.is_empty() {
            "Lynxer".to_string()
        } else {
            title.to_string()
        };
        state.width = if width > 0.0 { width } else { 800.0 };
        state.height = if height > 0.0 { height } else { 600.0 };
        state.initialized = true;
        state.headless = crate::state::headless_requested();
    });
}

pub fn set_resizable(value: bool) {
    with(|state| state.resizable = value);
}

pub fn set_fullscreen(value: bool) {
    with(|state| {
        state.fullscreen = value;
        if !state.headless {
            macroquad::window::set_fullscreen(value);
        }
    });
}

pub fn set_high_dpi(value: bool) {
    with(|state| state.high_dpi = value);
}

pub fn set_sample_count(value: i64) {
    with(|state| state.sample_count = (value as i32).max(1));
}

pub fn set_default_filter_mode(nearest: bool) {
    with(|state| {
        state.nearest_filter = nearest;
        if !state.headless {
            set_filter_mode(if nearest {
                FilterMode::Nearest
            } else {
                FilterMode::Linear
            });
        }
    });
}

pub fn set_target_fps(cap: f32) {
    with(|state| state.fps_cap = cap.max(0.0));
}

pub fn set_background(r: i64, g: i64, b: i64, a: i64) {
    let color = color_of_a(r, g, b, a);
    with(|state| state.background = color);
}

pub fn set_start_callback(name: &str) {
    with(|state| state.start_callback = name.to_string());
}

pub fn set_update_callback(name: &str) {
    with(|state| state.update_callback = name.to_string());
}

pub fn set_draw_callback(name: &str) {
    with(|state| state.draw_callback = name.to_string());
}

pub fn stop() {
    with(|state| state.quit_requested = true);
}

pub fn is_running() -> bool {
    with(|state| state.running)
}

pub fn screen_width() -> f32 {
    with(|state| {
        if state.headless {
            state.width
        } else {
            macroquad::window::screen_width()
        }
    })
}

pub fn screen_height() -> f32 {
    with(|state| {
        if state.headless {
            state.height
        } else {
            macroquad::window::screen_height()
        }
    })
}

pub fn dpi_scale() -> f32 {
    with(|state| {
        if state.headless {
            1.0
        } else {
            screen_dpi_scale()
        }
    })
}

pub fn request_screen_size(width: f32, height: f32) {
    with(|state| {
        state.width = width.max(1.0);
        state.height = height.max(1.0);
        if !state.headless {
            request_new_screen_size(width, height);
        }
    });
}

pub fn set_window_size(width: i64, height: i64) {
    with(|state| {
        state.width = (width.max(1)) as f32;
        state.height = (height.max(1)) as f32;
        if !state.headless {
            macroquad::miniquad::window::set_window_size(width.max(1) as u32, height.max(1) as u32);
        }
    });
}

pub fn set_window_position(x: i64, y: i64) {
    with(|state| {
        if !state.headless {
            macroquad::miniquad::window::set_window_position(x.max(0) as u32, y.max(0) as u32);
        }
    });
}

pub fn window_position() -> String {
    with(|state| {
        if state.headless {
            return "[0,0]".to_string();
        }
        let (x, y) = get_window_position();
        format!("[{x},{y}]")
    })
}

pub fn show_cursor(shown: bool) {
    with(|state| {
        if !state.headless {
            show_mouse(shown);
        }
    });
}

pub fn grab_cursor(grab: bool) {
    with(|state| {
        if !state.headless {
            set_cursor_grab(grab);
        }
    });
}

/// `kind` indexes [`CursorIcon`]; unknown values fall back to `Default`.
pub fn set_cursor(kind: i64) {
    let icon = match kind {
        1 => CursorIcon::Help,
        2 => CursorIcon::Pointer,
        3 => CursorIcon::Wait,
        4 => CursorIcon::Crosshair,
        5 => CursorIcon::Text,
        6 => CursorIcon::Move,
        7 => CursorIcon::NotAllowed,
        8 => CursorIcon::EWResize,
        9 => CursorIcon::NSResize,
        10 => CursorIcon::NESWResize,
        11 => CursorIcon::NWSEResize,
        _ => CursorIcon::Default,
    };
    with(|state| {
        if !state.headless {
            set_mouse_cursor(icon);
        }
    });
}

pub fn clipboard() -> String {
    with(|state| {
        if state.headless {
            return String::new();
        }
        clipboard_get().unwrap_or_default()
    })
}

pub fn set_clipboard(text: &str) {
    with(|state| {
        if !state.headless {
            clipboard_set(text);
        }
    });
}

pub fn quit_requested() -> bool {
    with(|state| {
        if state.headless {
            state.quit_requested
        } else {
            state.quit_requested || is_quit_requested()
        }
    })
}

pub fn request_close() {
    with(|state| {
        state.quit_requested = true;
        if !state.headless {
            request_quit();
        }
    });
}

pub fn prevent_close() {
    with(|state| {
        if !state.headless {
            prevent_quit();
        }
    });
}

pub fn screenshot(path: &str) -> i64 {
    if path.is_empty() {
        return -1;
    }
    // The framebuffer is read outside `with` because reading it borrows the
    // state again, and the cell is not re-entrant.
    if with(|state| state.headless) {
        let Some((bytes, width, height)) = crate::raster::image_bytes() else {
            // Nothing has been drawn yet, so there is no frame to write.
            return -1;
        };
        let image = macroquad::texture::Image {
            bytes,
            width,
            height,
        };
        // macroquad's `export_png` reports failure by panicking, which the
        // export guard turns into -1.
        image.export_png(path);
        return 0;
    }
    // macroquad's `export_png` reports failure by panicking, which the export
    // guard turns into -1.
    macroquad::texture::get_screen_data().export_png(path);
    0
}

pub fn version() -> String {
    format!(
        "lynxer_graphics {} / macroquad 0.4",
        env!("CARGO_PKG_VERSION")
    )
}

pub fn delta_time() -> f32 {
    with(|state| {
        if state.headless {
            state.dt as f32
        } else {
            get_frame_time()
        }
    })
}

pub fn elapsed() -> f32 {
    with(|state| {
        if state.headless {
            state.sim_time as f32
        } else {
            get_time() as f32
        }
    })
}

pub fn fps() -> i64 {
    with(|state| if state.headless { 60 } else { get_fps() as i64 })
}

/// macroquad's own `draw_fps()` has no position argument, so draw the count
/// ourselves to keep the documented `(x, y)` signature.
pub fn draw_fps(x: f32, y: f32) {
    with(|state| {
        if state.headless {
            return;
        }
        let text = format!("{}", get_fps());
        macroquad::text::draw_text(&text, x, y, 30.0, macroquad::color::WHITE);
    });
}
