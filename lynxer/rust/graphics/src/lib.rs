//! macroquad backend for the Lynxer `graphics` stdlib module.
//!
//! This crate is the whole module: it exports `lynxer_module_init_v1`, every op,
//! and `lynxer_module_attach_v1`. There is no C++ shim. Macroquad owns the
//! window and event loop; once per frame it invokes the registered Lynxer
//! `update(dt)` and `draw()` callbacks through the host API.
//!
//! `graphics` is the general-purpose drawing/window/input/UI surface and is
//! deliberately separate from the `game` module, which keeps its game-flavoured
//! layer (sprites, scenes, tilemaps, physics) over the same backend.
//!
//! Set `LYNXER_GRAPHICS_HEADLESS=1` to run without a window: `run()` executes a
//! fixed number of deterministic frames and every op that would touch the GL
//! context returns a placeholder instead.

mod camera;
mod font;
mod host;
mod input;
mod material;
mod misc;
mod raster;
mod shapes;
mod state;
mod text_ops;
mod texture_ops;
mod three;
mod turtle;
mod ui;
mod window;

use lynxer_abi::{
    export_float as abi_export_float, export_int as abi_export_int,
    export_string as abi_export_string, Args, LynxerHostApi,
};
use macroquad::color::Color;
use macroquad::conf::Conf;
use macroquad::miniquad::conf::Conf as MiniquadConf;
use macroquad::texture::Texture2D;
use macroquad::window::{clear_background, next_frame};
use macroquad::Window;

use crate::state::{with, HEADLESS_DT, HEADLESS_FRAMES};

/// Builds a colour from four 0..255 channels starting at numeric index `base`.
fn color_at(args: &Args, base: usize) -> Color {
    state::color_of_a(
        args.int(base),
        args.int(base + 1),
        args.int(base + 2),
        args.int(base + 3),
    )
}

fn texture_at(handle: i64) -> Option<Texture2D> {
    with(|state| state.texture(handle).cloned())
}

/// Runs the Lynxer update and draw callbacks for one frame. Returns false when
/// a callback failed, so the loop stops and the interpreter can rethrow.
fn run_frame(host: &'static LynxerHostApi, update: &str, draw: &str, dt: f64) -> bool {
    if !update.is_empty() && invoke_callback(host, update, Some(dt)) != 0 {
        return false;
    }
    if !draw.is_empty() && invoke_callback(host, draw, None) != 0 {
        return false;
    }
    true
}

fn invoke_callback(host: &'static LynxerHostApi, name: &str, dt: Option<f64>) -> i32 {
    with(|state| state.callback_active = true);
    let result = lynxer_abi::invoke(host, name, dt);
    with(|state| state.callback_active = false);
    result
}

fn requires_graphics_context(name: &str) -> bool {
    name.starts_with("lynxer_graphics_draw_")
        || name == "lynxer_graphics_clear_background"
        || name.starts_with("lynxer_graphics_load_")
        || name.starts_with("lynxer_graphics_texture_draw_")
        || name.starts_with("lynxer_graphics_render_target")
        || name.starts_with("lynxer_graphics_set_render_target")
        || name == "lynxer_graphics_end_render_target"
        || name.starts_with("lynxer_graphics_set_camera")
        || name == "lynxer_graphics_set_default_camera"
        || name == "lynxer_graphics_push_camera_state"
        || name == "lynxer_graphics_pop_camera_state"
        || name.starts_with("lynxer_graphics_use_material")
        || name == "lynxer_graphics_use_default_material"
        || name.starts_with("lynxer_graphics_set_uniform")
        || name == "lynxer_graphics_set_material_texture"
        || name == "lynxer_graphics_set_texture_filter"
        || name == "lynxer_graphics_build_textures_atlas"
        || name == "lynxer_graphics_texture_from_image"
        || name == "lynxer_graphics_image_from_texture"
        || name == "lynxer_graphics_get_screen_data"
        || name == "lynxer_graphics_screenshot"
        || name == "lynxer_graphics_set_default_filter_mode"
        || name == "lynxer_graphics_turtle_forward"
        || name == "lynxer_graphics_turtle_back"
        || name == "lynxer_graphics_turtle_goto"
        || (name.starts_with("lynxer_graphics_ui_")
            && matches!(
                name,
                "lynxer_graphics_ui_label"
                    | "lynxer_graphics_ui_label_at"
                    | "lynxer_graphics_ui_button"
                    | "lynxer_graphics_ui_button_at"
                    | "lynxer_graphics_ui_checkbox"
                    | "lynxer_graphics_ui_slider"
                    | "lynxer_graphics_ui_input_text"
                    | "lynxer_graphics_ui_input_password"
                    | "lynxer_graphics_ui_progress_bar"
                    | "lynxer_graphics_ui_combo_box"
                    | "lynxer_graphics_ui_separator"
                    | "lynxer_graphics_ui_same_line"
                    | "lynxer_graphics_ui_window_end"
                    | "lynxer_graphics_ui_group_end"
            ))
}

fn graphics_context_allowed(name: &str) -> bool {
    with(|state| {
        state.headless
            || state.callback_active
            || (state.buffering()
                && matches!(
                    name,
                    "lynxer_graphics_ui_label"
                        | "lynxer_graphics_ui_label_at"
                        | "lynxer_graphics_ui_button"
                        | "lynxer_graphics_ui_button_at"
                        | "lynxer_graphics_ui_checkbox"
                        | "lynxer_graphics_ui_slider"
                        | "lynxer_graphics_ui_input_text"
                        | "lynxer_graphics_ui_input_password"
                        | "lynxer_graphics_ui_progress_bar"
                        | "lynxer_graphics_ui_combo_box"
                        | "lynxer_graphics_ui_separator"
                        | "lynxer_graphics_ui_same_line"
                ))
            || (state.ui_stack.len() > 1
                && matches!(
                    name,
                    "lynxer_graphics_ui_window_end" | "lynxer_graphics_ui_group_end"
                ))
    })
}

macro_rules! export_int {
    ($name:ident, $args:ident, $body:block) => {
        abi_export_int!($name, $args, {
            if requires_graphics_context(stringify!($name))
                && !graphics_context_allowed(stringify!($name))
            {
                -1
            } else {
                $body
            }
        });
    };
}

macro_rules! export_float {
    ($name:ident, $args:ident, $body:block) => {
        abi_export_float!($name, $args, {
            if requires_graphics_context(stringify!($name))
                && !graphics_context_allowed(stringify!($name))
            {
                -1.0
            } else {
                $body
            }
        });
    };
}

macro_rules! export_string {
    ($name:ident, $args:ident, $body:block) => {
        abi_export_string!($name, $args, {
            if requires_graphics_context(stringify!($name))
                && !graphics_context_allowed(stringify!($name))
            {
                String::new()
            } else {
                $body
            }
        });
    };
}

export_int!(lynxer_graphics_init, args, {
    let title = args.string(0).to_string();
    window::init(&title, args.float(0), args.float(1));
    0
});

export_int!(lynxer_graphics_run, args, {
    let _ = args;
    let host = match host::host() {
        Some(host) => host,
        None => return -1,
    };
    let (
        headless,
        title,
        width,
        height,
        resizable,
        fullscreen,
        high_dpi,
        sample_count,
        fps_cap,
        start,
        update,
        draw,
    ) = with(|state| {
        (
            state.headless,
            state.title.clone(),
            state.width,
            state.height,
            state.resizable,
            state.fullscreen,
            state.high_dpi,
            state.sample_count,
            state.fps_cap,
            state.start_callback.clone(),
            state.update_callback.clone(),
            state.draw_callback.clone(),
        )
    });

    if headless {
        with(|state| state.running = true);
        if !start.is_empty() {
            invoke_callback(host, &start, None);
        }
        for _ in 0..HEADLESS_FRAMES {
            with(|state| {
                state.dt = HEADLESS_DT;
                state.sim_time += HEADLESS_DT;
                state.frames += 1;
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
        miniquad_conf: MiniquadConf {
            window_title: title,
            window_width: width as i32,
            window_height: height as i32,
            window_resizable: resizable,
            fullscreen,
            high_dpi,
            sample_count,
            ..Default::default()
        },
        ..Default::default()
    };

    Window::from_config(configuration, async move {
        with(|state| state.running = true);
        if !start.is_empty() {
            invoke_callback(host, &start, None);
        }
        loop {
            let dt = macroquad::time::get_frame_time() as f64;
            let (mouse_x, mouse_y) = macroquad::input::mouse_position();
            let (scroll_x, scroll_y) = macroquad::input::mouse_wheel();
            with(|state| {
                state.dt = dt;
                state.sim_time += dt;
                state.frames += 1;
                state.width = macroquad::window::screen_width();
                state.height = macroquad::window::screen_height();
                state.mouse_x = mouse_x;
                state.mouse_y = mouse_y;
                state.scroll_x = scroll_x;
                state.scroll_y = scroll_y;
            });
            let background = with(|state| state.background);
            clear_background(background);
            let keep_going = run_frame(host, &update, &draw, dt);
            if !keep_going || lynxer_abi::interrupted(host) || with(|state| state.quit_requested) {
                break;
            }
            if fps_cap > 0.0 {
                std::thread::sleep(std::time::Duration::from_secs_f64(1.0 / fps_cap as f64));
            }
            next_frame().await;
        }
        with(|state| state.running = false);
    });
    0
});

export_int!(lynxer_graphics_set_resizable, args, {
    window::set_resizable(args.bool(0));
    0
});

export_int!(lynxer_graphics_set_fullscreen, args, {
    window::set_fullscreen(args.bool(0));
    0
});

export_int!(lynxer_graphics_set_high_dpi, args, {
    window::set_high_dpi(args.bool(0));
    0
});

export_int!(lynxer_graphics_set_sample_count, args, {
    window::set_sample_count(args.int(0));
    0
});

export_int!(lynxer_graphics_set_default_filter_mode, args, {
    window::set_default_filter_mode(args.bool(0));
    0
});

export_int!(lynxer_graphics_set_target_fps, args, {
    window::set_target_fps(args.float(0));
    0
});

export_int!(lynxer_graphics_set_background, args, {
    window::set_background(args.int(0), args.int(1), args.int(2), args.int(3));
    0
});

export_int!(lynxer_graphics_set_start_callback, args, {
    window::set_start_callback(args.string(0));
    0
});

export_int!(lynxer_graphics_set_update_callback, args, {
    window::set_update_callback(args.string(0));
    0
});

export_int!(lynxer_graphics_set_draw_callback, args, {
    window::set_draw_callback(args.string(0));
    0
});

export_int!(lynxer_graphics_stop, args, {
    let _ = args;
    window::stop();
    0
});

export_int!(lynxer_graphics_is_running, args, {
    let _ = args;
    if window::is_running() {
        1
    } else {
        0
    }
});

export_float!(lynxer_graphics_screen_width, args, {
    let _ = args;
    window::screen_width() as f64
});

export_float!(lynxer_graphics_screen_height, args, {
    let _ = args;
    window::screen_height() as f64
});

export_float!(lynxer_graphics_dpi_scale, args, {
    let _ = args;
    window::dpi_scale() as f64
});

export_int!(lynxer_graphics_request_screen_size, args, {
    window::request_screen_size(args.float(0), args.float(1));
    0
});

export_int!(lynxer_graphics_set_window_size, args, {
    window::set_window_size(args.int(0), args.int(1));
    0
});

export_int!(lynxer_graphics_set_window_position, args, {
    window::set_window_position(args.int(0), args.int(1));
    0
});

export_string!(lynxer_graphics_window_position, args, {
    let _ = args;
    window::window_position()
});

export_int!(lynxer_graphics_show_mouse, args, {
    window::show_cursor(args.bool(0));
    0
});

export_int!(lynxer_graphics_set_cursor_grab, args, {
    window::grab_cursor(args.bool(0));
    0
});

export_int!(lynxer_graphics_set_mouse_cursor, args, {
    window::set_cursor(args.int(0));
    0
});

export_string!(lynxer_graphics_clipboard_get, args, {
    let _ = args;
    window::clipboard()
});

export_int!(lynxer_graphics_set_clipboard, args, {
    window::set_clipboard(args.string(0));
    0
});

export_int!(lynxer_graphics_quit_requested, args, {
    let _ = args;
    if window::quit_requested() {
        1
    } else {
        0
    }
});

export_int!(lynxer_graphics_request_quit, args, {
    let _ = args;
    window::request_close();
    0
});

export_int!(lynxer_graphics_prevent_quit, args, {
    let _ = args;
    window::prevent_close();
    0
});

export_int!(lynxer_graphics_screenshot, args, {
    let path = args.string(0).to_string();
    window::screenshot(&path)
});

export_string!(lynxer_graphics_version, args, {
    let _ = args;
    window::version()
});

export_float!(lynxer_graphics_delta_time, args, {
    let _ = args;
    window::delta_time() as f64
});

export_float!(lynxer_graphics_time, args, {
    let _ = args;
    window::elapsed() as f64
});

export_int!(lynxer_graphics_fps, args, {
    let _ = args;
    window::fps()
});

export_int!(lynxer_graphics_draw_fps, args, {
    window::draw_fps(args.float(0), args.float(1));
    0
});

export_int!(lynxer_graphics_clear_background, args, {
    shapes::clear_background(color_at(&args, 0));
    0
});

export_int!(lynxer_graphics_draw_line, args, {
    shapes::line(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        color_at(&args, 5),
    );
    0
});

export_int!(lynxer_graphics_draw_triangle, args, {
    shapes::triangle(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_triangle_lines, args, {
    shapes::triangle_lines(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        args.float(6),
        color_at(&args, 7),
    );
    0
});

export_int!(lynxer_graphics_draw_rectangle, args, {
    shapes::rectangle(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        color_at(&args, 4),
    );
    0
});

export_int!(lynxer_graphics_draw_rectangle_lines, args, {
    shapes::rectangle_lines(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        color_at(&args, 5),
    );
    0
});

export_int!(lynxer_graphics_draw_rectangle_rotated, args, {
    shapes::rectangle_rotated(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        args.float(6),
        color_at(&args, 7),
    );
    0
});

export_int!(lynxer_graphics_draw_rectangle_lines_rotated, args, {
    shapes::rectangle_lines_rotated(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        args.float(6),
        args.float(7),
        color_at(&args, 8),
    );
    0
});

export_int!(lynxer_graphics_draw_poly, args, {
    shapes::poly(
        args.float(0),
        args.float(1),
        args.int(2),
        args.float(3),
        args.float(4),
        color_at(&args, 5),
    );
    0
});

export_int!(lynxer_graphics_draw_poly_lines, args, {
    shapes::poly_lines(
        args.float(0),
        args.float(1),
        args.int(2),
        args.float(3),
        args.float(4),
        args.float(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_circle, args, {
    shapes::circle(
        args.float(0),
        args.float(1),
        args.float(2),
        color_at(&args, 3),
    );
    0
});

export_int!(lynxer_graphics_draw_circle_lines, args, {
    shapes::circle_lines(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        color_at(&args, 4),
    );
    0
});

export_int!(lynxer_graphics_draw_ellipse, args, {
    shapes::ellipse(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        color_at(&args, 5),
    );
    0
});

export_int!(lynxer_graphics_draw_ellipse_lines, args, {
    shapes::ellipse_lines(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_arc, args, {
    shapes::arc(
        args.float(0),
        args.float(1),
        args.int(2),
        args.float(3),
        args.float(4),
        args.float(5),
        args.float(6),
        color_at(&args, 7),
    );
    0
});

export_int!(lynxer_graphics_draw_hexagon, args, {
    shapes::hexagon(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.bool(4),
        color_at(&args, 5),
    );
    0
});

export_int!(lynxer_graphics_load_font, args, {
    let path = args.string(0).to_string();
    text_ops::load_font(&path)
});

export_int!(lynxer_graphics_set_default_font, args, {
    text_ops::set_default(args.int(0))
});

export_int!(lynxer_graphics_draw_text, args, {
    let content = args.string(0).to_string();
    text_ops::text(
        &content,
        args.float(0),
        args.float(1),
        args.float(2),
        color_at(&args, 3),
    );
    0
});

export_int!(lynxer_graphics_draw_text_ex, args, {
    let content = args.string(0).to_string();
    text_ops::text_ex(
        args.int(0),
        &content,
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_multiline_text, args, {
    let content = args.string(0).to_string();
    text_ops::multiline(
        &content,
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        color_at(&args, 4),
    );
    0
});

export_int!(lynxer_graphics_draw_multiline_text_ex, args, {
    let content = args.string(0).to_string();
    text_ops::multiline_ex(
        args.int(0),
        &content,
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        args.float(6),
        color_at(&args, 7),
    );
    0
});

export_string!(lynxer_graphics_measure, args, {
    let content = args.string(0).to_string();
    text_ops::measure(&content, args.int(0), args.float(1), args.float(2))
});

export_float!(lynxer_graphics_measure_width, args, {
    let content = args.string(0).to_string();
    text_ops::measure_width(&content, args.int(0), args.float(1), args.float(2)) as f64
});

export_float!(lynxer_graphics_measure_height, args, {
    let content = args.string(0).to_string();
    text_ops::measure_height(&content, args.int(0), args.float(1), args.float(2)) as f64
});

export_string!(lynxer_graphics_measure_multiline, args, {
    let content = args.string(0).to_string();
    text_ops::measure_multiline(
        &content,
        args.int(0),
        args.float(1),
        args.float(2),
        args.float(3),
    )
});

export_string!(lynxer_graphics_text_center, args, {
    let content = args.string(0).to_string();
    text_ops::center(
        &content,
        args.int(0),
        args.float(1),
        args.float(2),
        args.float(3),
    )
});

export_string!(lynxer_graphics_wrap_text, args, {
    let content = args.string(0).to_string();
    text_ops::wrap(&content, args.int(0), args.float(1), args.float(2))
});

export_int!(lynxer_graphics_set_camera_2d, args, {
    camera::set_2d(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
    );
    0
});

export_int!(lynxer_graphics_set_camera_2d_rect, args, {
    camera::set_2d_rect(args.float(0), args.float(1), args.float(2), args.float(3));
    0
});

export_int!(lynxer_graphics_set_default_camera, args, {
    let _ = args;
    camera::set_default();
    0
});

export_int!(lynxer_graphics_push_camera_state, args, {
    let _ = args;
    camera::push_state();
    0
});

export_int!(lynxer_graphics_pop_camera_state, args, {
    let _ = args;
    camera::pop_state();
    0
});

export_string!(lynxer_graphics_screen_to_world, args, {
    camera::screen_to_world(args.float(0), args.float(1))
});

export_string!(lynxer_graphics_world_to_screen, args, {
    camera::world_to_screen(args.float(0), args.float(1))
});

export_int!(lynxer_graphics_set_camera_3d, args, {
    camera::set_3d(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        args.float(6),
        args.float(7),
        args.float(8),
        args.float(9),
        args.bool(10),
        args.float(11),
        args.float(12),
    );
    0
});

export_string!(lynxer_graphics_camera_target, args, {
    let _ = args;
    camera::target()
});

export_int!(lynxer_graphics_load_texture, args, {
    let path = args.string(0).to_string();
    texture_ops::load_texture(&path)
});

export_int!(lynxer_graphics_load_image, args, {
    let path = args.string(0).to_string();
    texture_ops::load_image(&path)
});

export_int!(lynxer_graphics_texture_from_image, args, {
    texture_ops::texture_from_image(args.int(0))
});

export_int!(lynxer_graphics_image_from_texture, args, {
    texture_ops::image_from_texture(args.int(0))
});

export_int!(lynxer_graphics_gen_image, args, {
    texture_ops::gen_image(
        args.int(0),
        args.int(1),
        args.int(2),
        args.int(3),
        args.int(4),
        args.int(5),
    )
});

export_int!(lynxer_graphics_image_width, args, {
    texture_ops::image_width(args.int(0))
});

export_int!(lynxer_graphics_image_height, args, {
    texture_ops::image_height(args.int(0))
});

export_string!(lynxer_graphics_image_get_pixel, args, {
    texture_ops::image_get_pixel(args.int(0), args.int(1), args.int(2))
});

export_int!(lynxer_graphics_image_set_pixel, args, {
    texture_ops::image_set_pixel(
        args.int(0),
        args.int(1),
        args.int(2),
        args.int(3),
        args.int(4),
        args.int(5),
        args.int(6),
    )
});

export_int!(lynxer_graphics_export_image, args, {
    let path = args.string(0).to_string();
    texture_ops::export_image(args.int(0), &path)
});

export_float!(lynxer_graphics_texture_width, args, {
    texture_ops::texture_width(args.int(0)) as f64
});

export_float!(lynxer_graphics_texture_height, args, {
    texture_ops::texture_height(args.int(0)) as f64
});

export_string!(lynxer_graphics_texture_size, args, {
    texture_ops::texture_size(args.int(0))
});

export_int!(lynxer_graphics_draw_texture, args, {
    // Packed numbers keep their argument order, so the leading texture handle
    // shifts `x`/`y` and the colour to index 1 and 3.
    texture_ops::draw(
        args.int(0),
        args.float(1),
        args.float(2),
        color_at(&args, 3),
    );
    0
});

export_int!(lynxer_graphics_draw_texture_scaled, args, {
    texture_ops::draw_scaled(
        args.int(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        color_at(&args, 5),
    );
    0
});

export_int!(lynxer_graphics_draw_texture_region, args, {
    texture_ops::draw_region(
        args.int(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        args.float(6),
        args.float(7),
        args.float(8),
        color_at(&args, 9),
    );
    0
});

export_int!(lynxer_graphics_draw_texture_rotated, args, {
    texture_ops::draw_rotated(
        args.int(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_set_texture_filter, args, {
    texture_ops::set_filter(args.int(0), args.bool(1));
    0
});

export_int!(lynxer_graphics_build_textures_atlas, args, {
    let _ = args;
    texture_ops::build_atlas();
    0
});

export_int!(lynxer_graphics_render_target, args, {
    texture_ops::new_render_target(args.int(0), args.int(1), false)
});

export_int!(lynxer_graphics_render_target_msaa, args, {
    texture_ops::new_render_target(args.int(0), args.int(1), true)
});

export_int!(lynxer_graphics_render_target_texture, args, {
    texture_ops::render_target_texture(args.int(0))
});

export_int!(lynxer_graphics_set_render_target, args, {
    texture_ops::set_render_target(args.int(0))
});

export_int!(lynxer_graphics_end_render_target, args, {
    let _ = args;
    texture_ops::end_render_target();
    0
});

export_int!(lynxer_graphics_get_screen_data, args, {
    let _ = args;
    texture_ops::screen_image()
});

export_int!(lynxer_graphics_is_key_down, args, {
    let key = args.string(0).to_string();
    if input::is_down(&key) {
        1
    } else {
        0
    }
});

export_int!(lynxer_graphics_is_key_pressed, args, {
    let key = args.string(0).to_string();
    if input::is_pressed(&key) {
        1
    } else {
        0
    }
});

export_int!(lynxer_graphics_is_key_released, args, {
    let key = args.string(0).to_string();
    if input::is_released(&key) {
        1
    } else {
        0
    }
});

export_int!(lynxer_graphics_any_key_down, args, {
    let _ = args;
    if input::any_down() {
        1
    } else {
        0
    }
});

export_string!(lynxer_graphics_last_key_pressed, args, {
    let _ = args;
    input::last_pressed()
});

export_string!(lynxer_graphics_keys_down, args, {
    let _ = args;
    input::keys_down()
});

export_string!(lynxer_graphics_keys_pressed, args, {
    let _ = args;
    input::keys_pressed()
});

export_string!(lynxer_graphics_keys_released, args, {
    let _ = args;
    input::keys_released()
});

export_string!(lynxer_graphics_char_pressed, args, {
    let _ = args;
    input::char_pressed()
});

export_int!(lynxer_graphics_key_code, args, {
    let key = args.string(0).to_string();
    input::key_code(&key)
        .map(|code| (code as u16) as i64)
        .unwrap_or(-1)
});

export_string!(lynxer_graphics_key_name, args, {
    input::key_name_for(args.int(0))
});

export_string!(lynxer_graphics_mouse_position, args, {
    let _ = args;
    input::mouse()
});

export_string!(lynxer_graphics_mouse_position_local, args, {
    let _ = args;
    input::mouse_local()
});

export_string!(lynxer_graphics_mouse_delta, args, {
    let _ = args;
    input::mouse_delta()
});

export_string!(lynxer_graphics_mouse_wheel, args, {
    let _ = args;
    input::wheel()
});

export_int!(lynxer_graphics_is_mouse_button_down, args, {
    if input::mouse_down(args.int(0)) {
        1
    } else {
        0
    }
});

export_int!(lynxer_graphics_is_mouse_button_pressed, args, {
    if input::mouse_pressed(args.int(0)) {
        1
    } else {
        0
    }
});

export_int!(lynxer_graphics_is_mouse_button_released, args, {
    if input::mouse_released(args.int(0)) {
        1
    } else {
        0
    }
});

export_string!(lynxer_graphics_touches, args, {
    let _ = args;
    input::touch_list()
});

export_int!(lynxer_graphics_clear_input_queue, args, {
    let _ = args;
    input::clear_queue();
    0
});

export_string!(lynxer_graphics_dropped_files, args, {
    let _ = args;
    input::dropped()
});

export_int!(lynxer_graphics_simulate_mouse_with_touch, args, {
    input::simulate_touch(args.bool(0));
    0
});

export_int!(lynxer_graphics_draw_line_3d, args, {
    three::line(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_cube, args, {
    three::cube(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_cube_wires, args, {
    three::cube_wires(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_sphere, args, {
    three::sphere(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        color_at(&args, 4),
    );
    0
});

export_int!(lynxer_graphics_draw_sphere_wires, args, {
    three::sphere_wires(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        color_at(&args, 4),
    );
    0
});

export_int!(lynxer_graphics_draw_sphere_ex, args, {
    three::sphere_ex(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.int(4),
        args.int(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_cylinder, args, {
    three::cylinder(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_cylinder_wires, args, {
    three::cylinder_wires(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_cylinder_ex, args, {
    three::cylinder_ex(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.int(5),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_plane, args, {
    three::plane(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        color_at(&args, 5),
    );
    0
});

export_int!(lynxer_graphics_draw_grid, args, {
    three::grid(
        args.int(0),
        args.float(1),
        color_at(&args, 2),
        color_at(&args, 6),
    );
    0
});

export_int!(lynxer_graphics_draw_affine_parallelogram, args, {
    three::parallelogram(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        args.float(6),
        args.float(7),
        args.float(8),
        color_at(&args, 9),
    );
    0
});

export_int!(lynxer_graphics_draw_affine_parallelepiped, args, {
    three::parallelepiped(
        args.float(0),
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
        args.float(5),
        args.float(6),
        args.float(7),
        args.float(8),
        args.float(9),
        args.float(10),
        args.float(11),
        color_at(&args, 12),
    );
    0
});

export_int!(lynxer_graphics_load_material, args, {
    let vertex = args.string(0).to_string();
    let fragment = args.string(1).to_string();
    match material::load(&vertex, &fragment) {
        Some(material) => with(|state| {
            state.materials.push(Some(material));
            (state.materials.len() - 1) as i64
        }),
        None => -1,
    }
});

export_int!(lynxer_graphics_load_material_from_file, args, {
    let vertex = args.string(0).to_string();
    let fragment = args.string(1).to_string();
    match material::load_from_files(&vertex, &fragment) {
        Some(material) => with(|state| {
            state.materials.push(Some(material));
            (state.materials.len() - 1) as i64
        }),
        None => -1,
    }
});

export_int!(lynxer_graphics_use_material, args, {
    let handle = args.int(0);
    match with(|state| state.material(handle).cloned()) {
        Some(material) => {
            material::use_material(&material);
            0
        }
        None => -1,
    }
});

export_int!(lynxer_graphics_use_default_material, args, {
    let _ = args;
    material::use_default();
    0
});

export_int!(lynxer_graphics_set_uniform, args, {
    let name = args.string(0).to_string();
    match with(|state| state.material(args.int(0)).cloned()) {
        Some(material) => {
            material::set_uniform(&material, &name, args.float(1));
            0
        }
        None => -1,
    }
});

export_int!(lynxer_graphics_set_uniform_array, args, {
    let name = args.string(0).to_string();
    let values = misc::parse_floats(args.string(1));
    match with(|state| state.material(args.int(0)).cloned()) {
        Some(material) => {
            material::set_uniform_array(&material, &name, &values);
            0
        }
        None => -1,
    }
});

export_int!(lynxer_graphics_set_material_texture, args, {
    let name = args.string(0).to_string();
    let texture = texture_at(args.int(1));
    match (with(|state| state.material(args.int(0)).cloned()), texture) {
        (Some(material), Some(texture)) => {
            material::set_texture(&material, &name, texture);
            0
        }
        _ => -1,
    }
});

export_int!(lynxer_graphics_srand, args, {
    misc::seed(args.int(0));
    0
});

export_int!(lynxer_graphics_rand, args, {
    let _ = args;
    misc::next()
});

export_float!(lynxer_graphics_gen_range, args, {
    misc::range(args.float(0), args.float(1)) as f64
});

export_int!(lynxer_graphics_gen_range_int, args, {
    misc::range_int(args.int(0), args.int(1))
});

export_string!(lynxer_graphics_rgb_to_hsl, args, {
    misc::to_hsl(args.int(0), args.int(1), args.int(2))
});

export_string!(lynxer_graphics_hsl_to_rgb, args, {
    let color = misc::from_hsl(args.float(0), args.float(1), args.float(2));
    format!(
        "[{},{},{},{}]",
        crate::state::clamp_channel((color.r * 255.0) as f64),
        crate::state::clamp_channel((color.g * 255.0) as f64),
        crate::state::clamp_channel((color.b * 255.0) as f64),
        crate::state::clamp_channel((color.a * 255.0) as f64)
    )
});

export_int!(lynxer_graphics_set_assets_folder, args, {
    let path = args.string(0).to_string();
    misc::set_assets_folder(&path);
    0
});

export_int!(lynxer_graphics_ui_label, args, {
    let text = args.string(0).to_string();
    ui::label(&text, None);
    0
});

export_int!(lynxer_graphics_ui_label_at, args, {
    let text = args.string(0).to_string();
    ui::label(&text, Some((args.float(0), args.float(1))));
    0
});

export_int!(lynxer_graphics_ui_button, args, {
    let text = args.string(0).to_string();
    if ui::button(args.int(0), &text, None) {
        1
    } else {
        0
    }
});

export_int!(lynxer_graphics_ui_button_at, args, {
    let text = args.string(0).to_string();
    if ui::button(args.int(0), &text, Some((args.float(1), args.float(2)))) {
        1
    } else {
        0
    }
});

export_int!(lynxer_graphics_ui_checkbox, args, {
    let label = args.string(0).to_string();
    if ui::checkbox(args.int(0), &label) {
        1
    } else {
        0
    }
});

export_int!(lynxer_graphics_ui_checkbox_value, args, {
    if ui::checkbox_value(args.int(0)) {
        1
    } else {
        0
    }
});

export_int!(lynxer_graphics_ui_set_checkbox_value, args, {
    ui::set_checkbox_value(args.int(0), args.bool(1));
    0
});

export_float!(lynxer_graphics_ui_slider, args, {
    let label = args.string(0).to_string();
    ui::slider(args.int(0), &label, args.float(1), args.float(2)) as f64
});

export_float!(lynxer_graphics_ui_slider_value, args, {
    ui::slider_value(args.int(0)) as f64
});

export_int!(lynxer_graphics_ui_set_slider_value, args, {
    ui::set_slider_value(args.int(0), args.float(1));
    0
});

export_string!(lynxer_graphics_ui_input_text, args, {
    let label = args.string(0).to_string();
    ui::input_text(args.int(0), &label, false)
});

export_string!(lynxer_graphics_ui_input_password, args, {
    let label = args.string(0).to_string();
    ui::input_text(args.int(0), &label, true)
});

export_string!(lynxer_graphics_ui_input_text_value, args, {
    ui::input_text_value(args.int(0))
});

export_int!(lynxer_graphics_ui_set_input_text, args, {
    let text = args.string(0).to_string();
    ui::set_input_text(args.int(0), &text);
    0
});

export_int!(lynxer_graphics_ui_progress_bar, args, {
    let label = args.string(0).to_string();
    ui::progress_bar(&label, args.float(0), args.float(1), args.float(2));
    0
});

export_int!(lynxer_graphics_ui_combo_box, args, {
    let label = args.string(0).to_string();
    let options = ui::parse_options(args.string(1));
    ui::combo_box(args.int(0), &label, &options)
});

export_string!(lynxer_graphics_ui_combo_box_value, args, {
    ui::combo_box_value(args.int(0))
});

export_int!(lynxer_graphics_ui_separator, args, {
    let _ = args;
    ui::separator();
    0
});

export_int!(lynxer_graphics_ui_same_line, args, {
    ui::same_line(args.float(0));
    0
});

export_int!(lynxer_graphics_ui_result, args, { ui::result(args.int(0)) });

export_int!(lynxer_graphics_ui_window_begin, args, {
    let title = args.string(0).to_string();
    ui::window_begin(
        args.int(0),
        &title,
        args.float(1),
        args.float(2),
        args.float(3),
        args.float(4),
    )
});

export_int!(lynxer_graphics_ui_window_end, args, {
    let _ = args;
    ui::window_end()
});

export_int!(lynxer_graphics_ui_group_begin, args, {
    // The leading handle shifts w/h to numeric indices 1 and 2.
    ui::group_begin(args.int(0), args.float(1), args.float(2))
});

export_int!(lynxer_graphics_ui_group_end, args, {
    let _ = args;
    ui::group_end()
});

// --- turtle -----------------------------------------------------------------
//
// The turtle API is handle-based: handle 0 (or negative) is the implicit
// default turtle the pure-Lynxer `turtle` module drives, and `turtleCreate`
// hands out explicit handles from 1 up. The leading handle shifts the numeric
// arguments, so every coordinate/time reads from index 1.

export_int!(lynxer_graphics_turtle_create, args, {
    turtle::create(args.float(0), args.float(1), args.float(2))
});

export_int!(lynxer_graphics_turtle_forward, args, {
    turtle::move_by(args.int(0), args.float(1))
});

export_int!(lynxer_graphics_turtle_back, args, {
    turtle::move_by(args.int(0), -args.float(1))
});

export_int!(lynxer_graphics_turtle_turn_left, args, {
    turtle::turn(args.int(0), -args.float(1))
});

export_int!(lynxer_graphics_turtle_turn_right, args, {
    turtle::turn(args.int(0), args.float(1))
});

export_int!(lynxer_graphics_turtle_goto, args, {
    turtle::goto(args.int(0), args.float(1), args.float(2))
});

export_int!(lynxer_graphics_turtle_home, args, {
    turtle::home(args.int(0))
});

export_int!(lynxer_graphics_turtle_pen_up, args, {
    turtle::set_pen(args.int(0), false)
});

export_int!(lynxer_graphics_turtle_pen_down, args, {
    turtle::set_pen(args.int(0), true)
});

export_int!(lynxer_graphics_turtle_set_color, args, {
    turtle::set_color(args.int(0), color_at(&args, 1))
});

export_int!(lynxer_graphics_turtle_set_width, args, {
    turtle::set_width(args.int(0), args.float(1))
});

export_float!(lynxer_graphics_turtle_x, args, {
    turtle::position(args.int(0))
        .map(|p| p.0 as f64)
        .unwrap_or(0.0)
});

export_float!(lynxer_graphics_turtle_y, args, {
    turtle::position(args.int(0))
        .map(|p| p.1 as f64)
        .unwrap_or(0.0)
});

export_float!(lynxer_graphics_turtle_heading, args, {
    turtle::heading(args.int(0)) as f64
});

export_int!(lynxer_graphics_turtle_pen_is_down, args, {
    turtle::pen_is_down(args.int(0)) as i64
});

export_int!(lynxer_graphics_turtle_remove, args, {
    turtle::remove(args.int(0)) as i64
});

const OPS: &[(&str, &str, &str)] = &[
    ("init", "lynxer_graphics_init", "cdecl:int64(...)"),
    ("run", "lynxer_graphics_run", "cdecl:int64(...)"),
    (
        "setResizable",
        "lynxer_graphics_set_resizable",
        "cdecl:int64(...)",
    ),
    (
        "setFullscreen",
        "lynxer_graphics_set_fullscreen",
        "cdecl:int64(...)",
    ),
    (
        "setHighDpi",
        "lynxer_graphics_set_high_dpi",
        "cdecl:int64(...)",
    ),
    (
        "setSampleCount",
        "lynxer_graphics_set_sample_count",
        "cdecl:int64(...)",
    ),
    (
        "setDefaultFilterMode",
        "lynxer_graphics_set_default_filter_mode",
        "cdecl:int64(...)",
    ),
    (
        "setTargetFps",
        "lynxer_graphics_set_target_fps",
        "cdecl:int64(...)",
    ),
    (
        "setBackground",
        "lynxer_graphics_set_background",
        "cdecl:int64(...)",
    ),
    (
        "setStartCallback",
        "lynxer_graphics_set_start_callback",
        "cdecl:int64(...)",
    ),
    (
        "setUpdateCallback",
        "lynxer_graphics_set_update_callback",
        "cdecl:int64(...)",
    ),
    (
        "setDrawCallback",
        "lynxer_graphics_set_draw_callback",
        "cdecl:int64(...)",
    ),
    ("stop", "lynxer_graphics_stop", "cdecl:int64(...)"),
    (
        "isRunning",
        "lynxer_graphics_is_running",
        "cdecl:int64(...)",
    ),
    (
        "screenWidth",
        "lynxer_graphics_screen_width",
        "cdecl:float64(...)",
    ),
    (
        "screenHeight",
        "lynxer_graphics_screen_height",
        "cdecl:float64(...)",
    ),
    (
        "dpiScale",
        "lynxer_graphics_dpi_scale",
        "cdecl:float64(...)",
    ),
    (
        "requestScreenSize",
        "lynxer_graphics_request_screen_size",
        "cdecl:int64(...)",
    ),
    (
        "setWindowSize",
        "lynxer_graphics_set_window_size",
        "cdecl:int64(...)",
    ),
    (
        "setWindowPosition",
        "lynxer_graphics_set_window_position",
        "cdecl:int64(...)",
    ),
    (
        "windowPosition",
        "lynxer_graphics_window_position",
        "cdecl:cstring(...)",
    ),
    (
        "showMouse",
        "lynxer_graphics_show_mouse",
        "cdecl:int64(...)",
    ),
    (
        "setCursorGrab",
        "lynxer_graphics_set_cursor_grab",
        "cdecl:int64(...)",
    ),
    (
        "setMouseCursor",
        "lynxer_graphics_set_mouse_cursor",
        "cdecl:int64(...)",
    ),
    (
        "clipboardGet",
        "lynxer_graphics_clipboard_get",
        "cdecl:cstring(...)",
    ),
    (
        "setClipboard",
        "lynxer_graphics_set_clipboard",
        "cdecl:int64(...)",
    ),
    (
        "quitRequested",
        "lynxer_graphics_quit_requested",
        "cdecl:int64(...)",
    ),
    (
        "requestQuit",
        "lynxer_graphics_request_quit",
        "cdecl:int64(...)",
    ),
    (
        "preventQuit",
        "lynxer_graphics_prevent_quit",
        "cdecl:int64(...)",
    ),
    (
        "screenshot",
        "lynxer_graphics_screenshot",
        "cdecl:int64(...)",
    ),
    ("version", "lynxer_graphics_version", "cdecl:cstring(...)"),
    (
        "deltaTime",
        "lynxer_graphics_delta_time",
        "cdecl:float64(...)",
    ),
    ("time", "lynxer_graphics_time", "cdecl:float64(...)"),
    ("fps", "lynxer_graphics_fps", "cdecl:int64(...)"),
    ("drawFps", "lynxer_graphics_draw_fps", "cdecl:int64(...)"),
    (
        "clearBackground",
        "lynxer_graphics_clear_background",
        "cdecl:int64(...)",
    ),
    ("drawLine", "lynxer_graphics_draw_line", "cdecl:int64(...)"),
    (
        "drawTriangle",
        "lynxer_graphics_draw_triangle",
        "cdecl:int64(...)",
    ),
    (
        "drawTriangleLines",
        "lynxer_graphics_draw_triangle_lines",
        "cdecl:int64(...)",
    ),
    (
        "drawRectangle",
        "lynxer_graphics_draw_rectangle",
        "cdecl:int64(...)",
    ),
    (
        "drawRectangleLines",
        "lynxer_graphics_draw_rectangle_lines",
        "cdecl:int64(...)",
    ),
    (
        "drawRectangleRotated",
        "lynxer_graphics_draw_rectangle_rotated",
        "cdecl:int64(...)",
    ),
    (
        "drawRectangleLinesRotated",
        "lynxer_graphics_draw_rectangle_lines_rotated",
        "cdecl:int64(...)",
    ),
    ("drawPoly", "lynxer_graphics_draw_poly", "cdecl:int64(...)"),
    (
        "drawPolyLines",
        "lynxer_graphics_draw_poly_lines",
        "cdecl:int64(...)",
    ),
    (
        "drawCircle",
        "lynxer_graphics_draw_circle",
        "cdecl:int64(...)",
    ),
    (
        "drawCircleLines",
        "lynxer_graphics_draw_circle_lines",
        "cdecl:int64(...)",
    ),
    (
        "drawEllipse",
        "lynxer_graphics_draw_ellipse",
        "cdecl:int64(...)",
    ),
    (
        "drawEllipseLines",
        "lynxer_graphics_draw_ellipse_lines",
        "cdecl:int64(...)",
    ),
    ("drawArc", "lynxer_graphics_draw_arc", "cdecl:int64(...)"),
    (
        "drawHexagon",
        "lynxer_graphics_draw_hexagon",
        "cdecl:int64(...)",
    ),
    ("loadFont", "lynxer_graphics_load_font", "cdecl:int64(...)"),
    (
        "setDefaultFont",
        "lynxer_graphics_set_default_font",
        "cdecl:int64(...)",
    ),
    ("drawText", "lynxer_graphics_draw_text", "cdecl:int64(...)"),
    (
        "drawTextEx",
        "lynxer_graphics_draw_text_ex",
        "cdecl:int64(...)",
    ),
    (
        "drawMultilineText",
        "lynxer_graphics_draw_multiline_text",
        "cdecl:int64(...)",
    ),
    (
        "drawMultilineTextEx",
        "lynxer_graphics_draw_multiline_text_ex",
        "cdecl:int64(...)",
    ),
    ("measure", "lynxer_graphics_measure", "cdecl:cstring(...)"),
    (
        "measureWidth",
        "lynxer_graphics_measure_width",
        "cdecl:float64(...)",
    ),
    (
        "measureHeight",
        "lynxer_graphics_measure_height",
        "cdecl:float64(...)",
    ),
    (
        "measureMultiline",
        "lynxer_graphics_measure_multiline",
        "cdecl:cstring(...)",
    ),
    (
        "textCenter",
        "lynxer_graphics_text_center",
        "cdecl:cstring(...)",
    ),
    (
        "wrapText",
        "lynxer_graphics_wrap_text",
        "cdecl:cstring(...)",
    ),
    (
        "setCamera2D",
        "lynxer_graphics_set_camera_2d",
        "cdecl:int64(...)",
    ),
    (
        "setCamera2DFromRect",
        "lynxer_graphics_set_camera_2d_rect",
        "cdecl:int64(...)",
    ),
    (
        "setDefaultCamera",
        "lynxer_graphics_set_default_camera",
        "cdecl:int64(...)",
    ),
    (
        "pushCameraState",
        "lynxer_graphics_push_camera_state",
        "cdecl:int64(...)",
    ),
    (
        "popCameraState",
        "lynxer_graphics_pop_camera_state",
        "cdecl:int64(...)",
    ),
    (
        "screenToWorld",
        "lynxer_graphics_screen_to_world",
        "cdecl:cstring(...)",
    ),
    (
        "worldToScreen",
        "lynxer_graphics_world_to_screen",
        "cdecl:cstring(...)",
    ),
    (
        "setCamera3D",
        "lynxer_graphics_set_camera_3d",
        "cdecl:int64(...)",
    ),
    (
        "cameraTarget",
        "lynxer_graphics_camera_target",
        "cdecl:cstring(...)",
    ),
    (
        "loadTexture",
        "lynxer_graphics_load_texture",
        "cdecl:int64(...)",
    ),
    (
        "loadImage",
        "lynxer_graphics_load_image",
        "cdecl:int64(...)",
    ),
    (
        "textureFromImage",
        "lynxer_graphics_texture_from_image",
        "cdecl:int64(...)",
    ),
    (
        "imageFromTexture",
        "lynxer_graphics_image_from_texture",
        "cdecl:int64(...)",
    ),
    ("genImage", "lynxer_graphics_gen_image", "cdecl:int64(...)"),
    (
        "imageWidth",
        "lynxer_graphics_image_width",
        "cdecl:int64(...)",
    ),
    (
        "imageHeight",
        "lynxer_graphics_image_height",
        "cdecl:int64(...)",
    ),
    (
        "imageGetPixel",
        "lynxer_graphics_image_get_pixel",
        "cdecl:cstring(...)",
    ),
    (
        "imageSetPixel",
        "lynxer_graphics_image_set_pixel",
        "cdecl:int64(...)",
    ),
    (
        "exportImage",
        "lynxer_graphics_export_image",
        "cdecl:int64(...)",
    ),
    (
        "textureWidth",
        "lynxer_graphics_texture_width",
        "cdecl:float64(...)",
    ),
    (
        "textureHeight",
        "lynxer_graphics_texture_height",
        "cdecl:float64(...)",
    ),
    (
        "textureSize",
        "lynxer_graphics_texture_size",
        "cdecl:cstring(...)",
    ),
    (
        "drawTexture",
        "lynxer_graphics_draw_texture",
        "cdecl:int64(...)",
    ),
    (
        "drawTextureScaled",
        "lynxer_graphics_draw_texture_scaled",
        "cdecl:int64(...)",
    ),
    (
        "drawTextureRegion",
        "lynxer_graphics_draw_texture_region",
        "cdecl:int64(...)",
    ),
    (
        "drawTextureRotated",
        "lynxer_graphics_draw_texture_rotated",
        "cdecl:int64(...)",
    ),
    (
        "setTextureFilter",
        "lynxer_graphics_set_texture_filter",
        "cdecl:int64(...)",
    ),
    (
        "buildTexturesAtlas",
        "lynxer_graphics_build_textures_atlas",
        "cdecl:int64(...)",
    ),
    (
        "renderTarget",
        "lynxer_graphics_render_target",
        "cdecl:int64(...)",
    ),
    (
        "renderTargetMsaa",
        "lynxer_graphics_render_target_msaa",
        "cdecl:int64(...)",
    ),
    (
        "renderTargetTexture",
        "lynxer_graphics_render_target_texture",
        "cdecl:int64(...)",
    ),
    (
        "setRenderTarget",
        "lynxer_graphics_set_render_target",
        "cdecl:int64(...)",
    ),
    (
        "endRenderTarget",
        "lynxer_graphics_end_render_target",
        "cdecl:int64(...)",
    ),
    (
        "getScreenData",
        "lynxer_graphics_get_screen_data",
        "cdecl:int64(...)",
    ),
    (
        "isKeyDown",
        "lynxer_graphics_is_key_down",
        "cdecl:int64(...)",
    ),
    (
        "isKeyPressed",
        "lynxer_graphics_is_key_pressed",
        "cdecl:int64(...)",
    ),
    (
        "isKeyReleased",
        "lynxer_graphics_is_key_released",
        "cdecl:int64(...)",
    ),
    (
        "anyKeyDown",
        "lynxer_graphics_any_key_down",
        "cdecl:int64(...)",
    ),
    (
        "lastKeyPressed",
        "lynxer_graphics_last_key_pressed",
        "cdecl:cstring(...)",
    ),
    (
        "keysDown",
        "lynxer_graphics_keys_down",
        "cdecl:cstring(...)",
    ),
    (
        "keysPressed",
        "lynxer_graphics_keys_pressed",
        "cdecl:cstring(...)",
    ),
    (
        "keysReleased",
        "lynxer_graphics_keys_released",
        "cdecl:cstring(...)",
    ),
    (
        "charPressed",
        "lynxer_graphics_char_pressed",
        "cdecl:cstring(...)",
    ),
    ("keyCode", "lynxer_graphics_key_code", "cdecl:int64(...)"),
    ("keyName", "lynxer_graphics_key_name", "cdecl:cstring(...)"),
    (
        "mousePosition",
        "lynxer_graphics_mouse_position",
        "cdecl:cstring(...)",
    ),
    (
        "mousePositionLocal",
        "lynxer_graphics_mouse_position_local",
        "cdecl:cstring(...)",
    ),
    (
        "mouseDelta",
        "lynxer_graphics_mouse_delta",
        "cdecl:cstring(...)",
    ),
    (
        "mouseWheel",
        "lynxer_graphics_mouse_wheel",
        "cdecl:cstring(...)",
    ),
    (
        "isMouseButtonDown",
        "lynxer_graphics_is_mouse_button_down",
        "cdecl:int64(...)",
    ),
    (
        "isMouseButtonPressed",
        "lynxer_graphics_is_mouse_button_pressed",
        "cdecl:int64(...)",
    ),
    (
        "isMouseButtonReleased",
        "lynxer_graphics_is_mouse_button_released",
        "cdecl:int64(...)",
    ),
    ("touches", "lynxer_graphics_touches", "cdecl:cstring(...)"),
    (
        "clearInputQueue",
        "lynxer_graphics_clear_input_queue",
        "cdecl:int64(...)",
    ),
    (
        "droppedFiles",
        "lynxer_graphics_dropped_files",
        "cdecl:cstring(...)",
    ),
    (
        "simulateMouseWithTouch",
        "lynxer_graphics_simulate_mouse_with_touch",
        "cdecl:int64(...)",
    ),
    (
        "drawLine3D",
        "lynxer_graphics_draw_line_3d",
        "cdecl:int64(...)",
    ),
    ("drawCube", "lynxer_graphics_draw_cube", "cdecl:int64(...)"),
    (
        "drawCubeWires",
        "lynxer_graphics_draw_cube_wires",
        "cdecl:int64(...)",
    ),
    (
        "drawSphere",
        "lynxer_graphics_draw_sphere",
        "cdecl:int64(...)",
    ),
    (
        "drawSphereWires",
        "lynxer_graphics_draw_sphere_wires",
        "cdecl:int64(...)",
    ),
    (
        "drawSphereEx",
        "lynxer_graphics_draw_sphere_ex",
        "cdecl:int64(...)",
    ),
    (
        "drawCylinder",
        "lynxer_graphics_draw_cylinder",
        "cdecl:int64(...)",
    ),
    (
        "drawCylinderWires",
        "lynxer_graphics_draw_cylinder_wires",
        "cdecl:int64(...)",
    ),
    (
        "drawCylinderEx",
        "lynxer_graphics_draw_cylinder_ex",
        "cdecl:int64(...)",
    ),
    (
        "drawPlane",
        "lynxer_graphics_draw_plane",
        "cdecl:int64(...)",
    ),
    ("drawGrid", "lynxer_graphics_draw_grid", "cdecl:int64(...)"),
    (
        "drawAffineParallelogram",
        "lynxer_graphics_draw_affine_parallelogram",
        "cdecl:int64(...)",
    ),
    (
        "drawAffineParallelepiped",
        "lynxer_graphics_draw_affine_parallelepiped",
        "cdecl:int64(...)",
    ),
    (
        "loadMaterial",
        "lynxer_graphics_load_material",
        "cdecl:int64(...)",
    ),
    (
        "loadMaterialFromFile",
        "lynxer_graphics_load_material_from_file",
        "cdecl:int64(...)",
    ),
    (
        "useMaterial",
        "lynxer_graphics_use_material",
        "cdecl:int64(...)",
    ),
    (
        "useDefaultMaterial",
        "lynxer_graphics_use_default_material",
        "cdecl:int64(...)",
    ),
    (
        "setUniform",
        "lynxer_graphics_set_uniform",
        "cdecl:int64(...)",
    ),
    (
        "setUniformArray",
        "lynxer_graphics_set_uniform_array",
        "cdecl:int64(...)",
    ),
    (
        "setMaterialTexture",
        "lynxer_graphics_set_material_texture",
        "cdecl:int64(...)",
    ),
    ("srand", "lynxer_graphics_srand", "cdecl:int64(...)"),
    ("rand", "lynxer_graphics_rand", "cdecl:int64(...)"),
    (
        "genRange",
        "lynxer_graphics_gen_range",
        "cdecl:float64(...)",
    ),
    (
        "genRangeInt",
        "lynxer_graphics_gen_range_int",
        "cdecl:int64(...)",
    ),
    (
        "rgbToHsl",
        "lynxer_graphics_rgb_to_hsl",
        "cdecl:cstring(...)",
    ),
    (
        "hslToRgb",
        "lynxer_graphics_hsl_to_rgb",
        "cdecl:cstring(...)",
    ),
    (
        "setAssetsFolder",
        "lynxer_graphics_set_assets_folder",
        "cdecl:int64(...)",
    ),
    ("uiLabel", "lynxer_graphics_ui_label", "cdecl:int64(...)"),
    (
        "uiLabelAt",
        "lynxer_graphics_ui_label_at",
        "cdecl:int64(...)",
    ),
    ("uiButton", "lynxer_graphics_ui_button", "cdecl:int64(...)"),
    (
        "uiButtonAt",
        "lynxer_graphics_ui_button_at",
        "cdecl:int64(...)",
    ),
    (
        "uiCheckbox",
        "lynxer_graphics_ui_checkbox",
        "cdecl:int64(...)",
    ),
    (
        "uiCheckboxValue",
        "lynxer_graphics_ui_checkbox_value",
        "cdecl:int64(...)",
    ),
    (
        "uiSetCheckboxValue",
        "lynxer_graphics_ui_set_checkbox_value",
        "cdecl:int64(...)",
    ),
    (
        "uiSlider",
        "lynxer_graphics_ui_slider",
        "cdecl:float64(...)",
    ),
    (
        "uiSliderValue",
        "lynxer_graphics_ui_slider_value",
        "cdecl:float64(...)",
    ),
    (
        "uiSetSliderValue",
        "lynxer_graphics_ui_set_slider_value",
        "cdecl:int64(...)",
    ),
    (
        "uiInputText",
        "lynxer_graphics_ui_input_text",
        "cdecl:cstring(...)",
    ),
    (
        "uiInputPassword",
        "lynxer_graphics_ui_input_password",
        "cdecl:cstring(...)",
    ),
    (
        "uiInputTextValue",
        "lynxer_graphics_ui_input_text_value",
        "cdecl:cstring(...)",
    ),
    (
        "uiSetInputText",
        "lynxer_graphics_ui_set_input_text",
        "cdecl:int64(...)",
    ),
    (
        "uiProgressBar",
        "lynxer_graphics_ui_progress_bar",
        "cdecl:int64(...)",
    ),
    (
        "uiComboBox",
        "lynxer_graphics_ui_combo_box",
        "cdecl:int64(...)",
    ),
    (
        "uiComboBoxValue",
        "lynxer_graphics_ui_combo_box_value",
        "cdecl:cstring(...)",
    ),
    (
        "uiSeparator",
        "lynxer_graphics_ui_separator",
        "cdecl:int64(...)",
    ),
    (
        "uiSameLine",
        "lynxer_graphics_ui_same_line",
        "cdecl:int64(...)",
    ),
    ("uiResult", "lynxer_graphics_ui_result", "cdecl:int64(...)"),
    (
        "uiWindowBegin",
        "lynxer_graphics_ui_window_begin",
        "cdecl:int64(...)",
    ),
    (
        "uiWindowEnd",
        "lynxer_graphics_ui_window_end",
        "cdecl:int64(...)",
    ),
    (
        "uiGroupBegin",
        "lynxer_graphics_ui_group_begin",
        "cdecl:int64(...)",
    ),
    (
        "uiGroupEnd",
        "lynxer_graphics_ui_group_end",
        "cdecl:int64(...)",
    ),
    (
        "turtleCreate",
        "lynxer_graphics_turtle_create",
        "cdecl:int64(...)",
    ),
    (
        "turtleForward",
        "lynxer_graphics_turtle_forward",
        "cdecl:int64(...)",
    ),
    (
        "turtleBack",
        "lynxer_graphics_turtle_back",
        "cdecl:int64(...)",
    ),
    (
        "turtleTurnLeft",
        "lynxer_graphics_turtle_turn_left",
        "cdecl:int64(...)",
    ),
    (
        "turtleTurnRight",
        "lynxer_graphics_turtle_turn_right",
        "cdecl:int64(...)",
    ),
    (
        "turtleGoto",
        "lynxer_graphics_turtle_goto",
        "cdecl:int64(...)",
    ),
    (
        "turtleHome",
        "lynxer_graphics_turtle_home",
        "cdecl:int64(...)",
    ),
    (
        "turtlePenUp",
        "lynxer_graphics_turtle_pen_up",
        "cdecl:int64(...)",
    ),
    (
        "turtlePenDown",
        "lynxer_graphics_turtle_pen_down",
        "cdecl:int64(...)",
    ),
    (
        "turtleSetColor",
        "lynxer_graphics_turtle_set_color",
        "cdecl:int64(...)",
    ),
    (
        "turtleSetWidth",
        "lynxer_graphics_turtle_set_width",
        "cdecl:int64(...)",
    ),
    ("turtleX", "lynxer_graphics_turtle_x", "cdecl:float64(...)"),
    ("turtleY", "lynxer_graphics_turtle_y", "cdecl:float64(...)"),
    (
        "turtleHeading",
        "lynxer_graphics_turtle_heading",
        "cdecl:float64(...)",
    ),
    (
        "turtlePenIsDown",
        "lynxer_graphics_turtle_pen_is_down",
        "cdecl:int64(...)",
    ),
    (
        "turtleRemove",
        "lynxer_graphics_turtle_remove",
        "cdecl:int64(...)",
    ),
];

lynxer_abi::lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::{graphics_context_allowed, requires_graphics_context};
    use crate::state::with;

    #[test]
    fn gpu_operations_have_a_single_outside_callback_policy() {
        assert!(requires_graphics_context("lynxer_graphics_draw_cube"));
        assert!(requires_graphics_context("lynxer_graphics_load_texture"));
        assert!(!requires_graphics_context("lynxer_graphics_image_width"));
        let previous = with(|state| {
            let old_headless = state.headless;
            let old_callback = state.callback_active;
            state.headless = false;
            state.callback_active = false;
            (old_headless, old_callback)
        });
        assert!(!graphics_context_allowed("lynxer_graphics_draw_cube"));
        with(|state| state.callback_active = true);
        assert!(graphics_context_allowed("lynxer_graphics_draw_cube"));
        with(|state| {
            state.callback_active = true;
            state.callback_active = false;
            state.headless = true;
        });
        assert!(graphics_context_allowed("lynxer_graphics_draw_cube"));
        with(|state| {
            state.headless = previous.0;
            state.callback_active = previous.1;
        });
    }
}
