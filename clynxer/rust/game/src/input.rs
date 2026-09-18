//! Keyboard and mouse input.

use macroquad::input::{
    is_key_down, is_key_pressed, is_key_released, is_mouse_button_down, is_mouse_button_pressed,
    is_mouse_button_released, mouse_position, KeyCode, MouseButton,
};
use macroquad::window::screen_height;

use crate::state::with;

fn headless() -> bool {
    with(|state| state.headless)
}

fn key_code(name: &str) -> Option<KeyCode> {
    let upper = name.trim().to_ascii_uppercase();
    Some(match upper.as_str() {
        "UP" => KeyCode::Up,
        "DOWN" => KeyCode::Down,
        "LEFT" => KeyCode::Left,
        "RIGHT" => KeyCode::Right,
        "SPACE" => KeyCode::Space,
        "ENTER" | "RETURN" => KeyCode::Enter,
        "ESCAPE" | "ESC" => KeyCode::Escape,
        "TAB" => KeyCode::Tab,
        "BACKSPACE" => KeyCode::Backspace,
        "SHIFT" => KeyCode::LeftShift,
        "CTRL" | "CONTROL" => KeyCode::LeftControl,
        "ALT" => KeyCode::LeftAlt,
        "A" => KeyCode::A,
        "B" => KeyCode::B,
        "C" => KeyCode::C,
        "D" => KeyCode::D,
        "E" => KeyCode::E,
        "F" => KeyCode::F,
        "G" => KeyCode::G,
        "H" => KeyCode::H,
        "I" => KeyCode::I,
        "J" => KeyCode::J,
        "K" => KeyCode::K,
        "L" => KeyCode::L,
        "M" => KeyCode::M,
        "N" => KeyCode::N,
        "O" => KeyCode::O,
        "P" => KeyCode::P,
        "Q" => KeyCode::Q,
        "R" => KeyCode::R,
        "S" => KeyCode::S,
        "T" => KeyCode::T,
        "U" => KeyCode::U,
        "V" => KeyCode::V,
        "W" => KeyCode::W,
        "X" => KeyCode::X,
        "Y" => KeyCode::Y,
        "Z" => KeyCode::Z,
        "0" => KeyCode::Key0,
        "1" => KeyCode::Key1,
        "2" => KeyCode::Key2,
        "3" => KeyCode::Key3,
        "4" => KeyCode::Key4,
        "5" => KeyCode::Key5,
        "6" => KeyCode::Key6,
        "7" => KeyCode::Key7,
        "8" => KeyCode::Key8,
        "9" => KeyCode::Key9,
        "F1" => KeyCode::F1,
        "F2" => KeyCode::F2,
        "F3" => KeyCode::F3,
        "F4" => KeyCode::F4,
        "F5" => KeyCode::F5,
        "F6" => KeyCode::F6,
        "F7" => KeyCode::F7,
        "F8" => KeyCode::F8,
        "F9" => KeyCode::F9,
        "F10" => KeyCode::F10,
        "F11" => KeyCode::F11,
        "F12" => KeyCode::F12,
        _ => return None,
    })
}

fn mouse_button(name: &str) -> Option<MouseButton> {
    Some(match name.trim().to_ascii_uppercase().as_str() {
        "LEFT" => MouseButton::Left,
        "RIGHT" => MouseButton::Right,
        "MIDDLE" => MouseButton::Middle,
        _ => return None,
    })
}

game_export_int!(lynxer_game_key_down, args, {
    if headless() {
        return 0;
    }
    match key_code(args.string(0)) {
        Some(code) => is_key_down(code) as i64,
        None => 0,
    }
});

game_export_int!(lynxer_game_key_up, args, {
    if headless() {
        return 0;
    }
    match key_code(args.string(0)) {
        Some(code) => (!is_key_down(code)) as i64,
        None => 1,
    }
});

game_export_int!(lynxer_game_key_pressed, args, {
    if headless() {
        return 0;
    }
    match key_code(args.string(0)) {
        Some(code) => is_key_pressed(code) as i64,
        None => 0,
    }
});

game_export_int!(lynxer_game_key_released, args, {
    if headless() {
        return 0;
    }
    match key_code(args.string(0)) {
        Some(code) => is_key_released(code) as i64,
        None => 0,
    }
});

game_export_int!(lynxer_game_key_code, args, {
    match key_code(args.string(0)) {
        Some(code) => code as u16 as i64,
        None => -1,
    }
});

game_export_float!(lynxer_game_mouse_x, args, {
    let _ = args;
    if headless() {
        return 0.0;
    }
    mouse_position().0 as f64
});

game_export_float!(lynxer_game_mouse_y, args, {
    let _ = args;
    if headless() {
        return 0.0;
    }
    (screen_height() - mouse_position().1) as f64
});

game_export_float!(lynxer_game_mouse_delta_x, args, {
    let _ = args;
    if headless() {
        return 0.0;
    }
    let (x, _) = mouse_position();
    with(|state| {
        let delta = x - state.mouse_query_x;
        state.mouse_query_x = x;
        delta as f64
    })
});

game_export_float!(lynxer_game_mouse_delta_y, args, {
    let _ = args;
    if headless() {
        return 0.0;
    }
    let (_, y) = mouse_position();
    with(|state| {
        let delta = y - state.mouse_query_y;
        state.mouse_query_y = y;
        delta as f64
    })
});

game_export_int!(lynxer_game_mouse_left, args, {
    let _ = args;
    if headless() {
        return 0;
    }
    is_mouse_button_down(MouseButton::Left) as i64
});

game_export_int!(lynxer_game_mouse_right, args, {
    let _ = args;
    if headless() {
        return 0;
    }
    is_mouse_button_down(MouseButton::Right) as i64
});

game_export_int!(lynxer_game_mouse_middle, args, {
    let _ = args;
    if headless() {
        return 0;
    }
    is_mouse_button_down(MouseButton::Middle) as i64
});

game_export_int!(lynxer_game_mouse_button_down, args, {
    if headless() {
        return 0;
    }
    match mouse_button(args.string(0)) {
        Some(button) => is_mouse_button_down(button) as i64,
        None => 0,
    }
});

game_export_int!(lynxer_game_mouse_button_pressed, args, {
    if headless() {
        return 0;
    }
    match mouse_button(args.string(0)) {
        Some(button) => is_mouse_button_pressed(button) as i64,
        None => 0,
    }
});

game_export_int!(lynxer_game_mouse_button_released, args, {
    if headless() {
        return 0;
    }
    match mouse_button(args.string(0)) {
        Some(button) => is_mouse_button_released(button) as i64,
        None => 0,
    }
});

game_export_int!(lynxer_game_mouse_button_code, args, {
    match mouse_button(args.string(0)) {
        Some(MouseButton::Left) => 0,
        Some(MouseButton::Middle) => 1,
        Some(MouseButton::Right) => 2,
        _ => -1,
    }
});

game_export_float!(lynxer_game_mouse_scroll_y, args, {
    let _ = args;
    with(|state| {
        let value = state.scroll_y;
        state.scroll_y = 0.0;
        value as f64
    })
});

game_export_float!(lynxer_game_mouse_scroll_x, args, {
    let _ = args;
    with(|state| {
        let value = state.scroll_x;
        state.scroll_x = 0.0;
        value as f64
    })
});
