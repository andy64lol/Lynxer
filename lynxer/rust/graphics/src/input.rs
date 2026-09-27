//! Keyboard, mouse, touch and dropped-file input.
//!
//! Keys are addressed by name (`"space"`, `"leftshift"`, `"f1"`) rather than by
//! miniquad's numeric discriminants; [`key_code`] and [`key_name`] bridge the
//! two. Every query answers "nothing pressed" in headless mode.

use macroquad::input::{
    clear_input_queue, get_char_pressed, get_dropped_files, get_keys_down, get_keys_pressed,
    get_keys_released, get_last_key_pressed, is_any_key_down, is_key_down, is_key_pressed,
    is_key_released, is_mouse_button_down, is_mouse_button_pressed, is_mouse_button_released,
    mouse_delta_position, mouse_position, mouse_position_local, mouse_wheel,
    simulate_mouse_with_touch, touches, KeyCode, MouseButton, TouchPhase,
};

use crate::state::{number, with};

/// Canonical names first, then aliases: [`key_name`] takes the first match, so
/// canonical spellings win the reverse lookup.
const KEYS: &[(&str, KeyCode)] = &[
    ("space", KeyCode::Space),
    ("apostrophe", KeyCode::Apostrophe),
    ("quote", KeyCode::Apostrophe),
    ("comma", KeyCode::Comma),
    ("minus", KeyCode::Minus),
    ("period", KeyCode::Period),
    ("dot", KeyCode::Period),
    ("slash", KeyCode::Slash),
    ("key0", KeyCode::Key0),
    ("0", KeyCode::Key0),
    ("key1", KeyCode::Key1),
    ("1", KeyCode::Key1),
    ("key2", KeyCode::Key2),
    ("2", KeyCode::Key2),
    ("key3", KeyCode::Key3),
    ("3", KeyCode::Key3),
    ("key4", KeyCode::Key4),
    ("4", KeyCode::Key4),
    ("key5", KeyCode::Key5),
    ("5", KeyCode::Key5),
    ("key6", KeyCode::Key6),
    ("6", KeyCode::Key6),
    ("key7", KeyCode::Key7),
    ("7", KeyCode::Key7),
    ("key8", KeyCode::Key8),
    ("8", KeyCode::Key8),
    ("key9", KeyCode::Key9),
    ("9", KeyCode::Key9),
    ("semicolon", KeyCode::Semicolon),
    ("equal", KeyCode::Equal),
    ("a", KeyCode::A),
    ("b", KeyCode::B),
    ("c", KeyCode::C),
    ("d", KeyCode::D),
    ("e", KeyCode::E),
    ("f", KeyCode::F),
    ("g", KeyCode::G),
    ("h", KeyCode::H),
    ("i", KeyCode::I),
    ("j", KeyCode::J),
    ("k", KeyCode::K),
    ("l", KeyCode::L),
    ("m", KeyCode::M),
    ("n", KeyCode::N),
    ("o", KeyCode::O),
    ("p", KeyCode::P),
    ("q", KeyCode::Q),
    ("r", KeyCode::R),
    ("s", KeyCode::S),
    ("t", KeyCode::T),
    ("u", KeyCode::U),
    ("v", KeyCode::V),
    ("w", KeyCode::W),
    ("x", KeyCode::X),
    ("y", KeyCode::Y),
    ("z", KeyCode::Z),
    ("leftbracket", KeyCode::LeftBracket),
    ("backslash", KeyCode::Backslash),
    ("rightbracket", KeyCode::RightBracket),
    ("graveaccent", KeyCode::GraveAccent),
    ("grave", KeyCode::GraveAccent),
    ("backtick", KeyCode::GraveAccent),
    ("world1", KeyCode::World1),
    ("world2", KeyCode::World2),
    ("escape", KeyCode::Escape),
    ("esc", KeyCode::Escape),
    ("enter", KeyCode::Enter),
    ("return", KeyCode::Enter),
    ("tab", KeyCode::Tab),
    ("backspace", KeyCode::Backspace),
    ("insert", KeyCode::Insert),
    ("delete", KeyCode::Delete),
    ("del", KeyCode::Delete),
    ("right", KeyCode::Right),
    ("arrowright", KeyCode::Right),
    ("left", KeyCode::Left),
    ("arrowleft", KeyCode::Left),
    ("down", KeyCode::Down),
    ("arrowdown", KeyCode::Down),
    ("up", KeyCode::Up),
    ("arrowup", KeyCode::Up),
    ("pageup", KeyCode::PageUp),
    ("pgup", KeyCode::PageUp),
    ("pagedown", KeyCode::PageDown),
    ("pgdn", KeyCode::PageDown),
    ("home", KeyCode::Home),
    ("end", KeyCode::End),
    ("capslock", KeyCode::CapsLock),
    ("scrolllock", KeyCode::ScrollLock),
    ("numlock", KeyCode::NumLock),
    ("printscreen", KeyCode::PrintScreen),
    ("pause", KeyCode::Pause),
    ("f1", KeyCode::F1),
    ("f2", KeyCode::F2),
    ("f3", KeyCode::F3),
    ("f4", KeyCode::F4),
    ("f5", KeyCode::F5),
    ("f6", KeyCode::F6),
    ("f7", KeyCode::F7),
    ("f8", KeyCode::F8),
    ("f9", KeyCode::F9),
    ("f10", KeyCode::F10),
    ("f11", KeyCode::F11),
    ("f12", KeyCode::F12),
    ("f13", KeyCode::F13),
    ("f14", KeyCode::F14),
    ("f15", KeyCode::F15),
    ("f16", KeyCode::F16),
    ("f17", KeyCode::F17),
    ("f18", KeyCode::F18),
    ("f19", KeyCode::F19),
    ("f20", KeyCode::F20),
    ("f21", KeyCode::F21),
    ("f22", KeyCode::F22),
    ("f23", KeyCode::F23),
    ("f24", KeyCode::F24),
    ("f25", KeyCode::F25),
    ("kp0", KeyCode::Kp0),
    ("kp1", KeyCode::Kp1),
    ("kp2", KeyCode::Kp2),
    ("kp3", KeyCode::Kp3),
    ("kp4", KeyCode::Kp4),
    ("kp5", KeyCode::Kp5),
    ("kp6", KeyCode::Kp6),
    ("kp7", KeyCode::Kp7),
    ("kp8", KeyCode::Kp8),
    ("kp9", KeyCode::Kp9),
    ("kpdecimal", KeyCode::KpDecimal),
    ("kpdivide", KeyCode::KpDivide),
    ("kpmultiply", KeyCode::KpMultiply),
    ("kpsubtract", KeyCode::KpSubtract),
    ("kpadd", KeyCode::KpAdd),
    ("kpenter", KeyCode::KpEnter),
    ("kpequal", KeyCode::KpEqual),
    ("leftshift", KeyCode::LeftShift),
    ("shift", KeyCode::LeftShift),
    ("leftcontrol", KeyCode::LeftControl),
    ("ctrl", KeyCode::LeftControl),
    ("control", KeyCode::LeftControl),
    ("leftalt", KeyCode::LeftAlt),
    ("alt", KeyCode::LeftAlt),
    ("leftsuper", KeyCode::LeftSuper),
    ("super", KeyCode::LeftSuper),
    ("meta", KeyCode::LeftSuper),
    ("rightshift", KeyCode::RightShift),
    ("rightcontrol", KeyCode::RightControl),
    ("rightalt", KeyCode::RightAlt),
    ("rightsuper", KeyCode::RightSuper),
    ("menu", KeyCode::Menu),
    ("back", KeyCode::Back),
    ("unknown", KeyCode::Unknown),
];

/// Case- and separator-insensitive: `"Left Shift"`, `"left-shift"` and
/// `"leftshift"` all name the same key.
fn normalize(name: &str) -> String {
    name.chars()
        .filter(|character| character.is_ascii_alphanumeric())
        .flat_map(|character| character.to_lowercase())
        .collect()
}

pub fn key_code(name: &str) -> Option<KeyCode> {
    let wanted = normalize(name);
    KEYS.iter()
        .find(|(label, _)| *label == wanted)
        .map(|(_, code)| *code)
}

pub fn key_name(code: KeyCode) -> String {
    KEYS.iter()
        .find(|(_, candidate)| *candidate == code)
        .map(|(label, _)| (*label).to_string())
        .unwrap_or_default()
}

/// Looks a key up from its numeric discriminant. The table is searched rather
/// than transmuting the integer into a `KeyCode`, which would be undefined
/// behaviour for the discriminants with no variant.
pub fn key_name_for(code: i64) -> String {
    KEYS.iter()
        .find(|(_, candidate)| (*candidate as u16) as i64 == code)
        .map(|(label, _)| (*label).to_string())
        .unwrap_or_default()
}

fn headless() -> bool {
    with(|state| state.headless)
}

macro_rules! key_query {
    ($name:ident, $function:path) => {
        pub fn $name(key: &str) -> bool {
            if headless() {
                return false;
            }
            match key_code(key) {
                Some(code) => $function(code),
                None => false,
            }
        }
    };
}

key_query!(is_down, is_key_down);
key_query!(is_pressed, is_key_pressed);
key_query!(is_released, is_key_released);

pub fn any_down() -> bool {
    !headless() && is_any_key_down()
}

pub fn last_pressed() -> String {
    if headless() {
        return String::new();
    }
    get_last_key_pressed().map(key_name).unwrap_or_default()
}

fn key_set(names: Vec<KeyCode>) -> String {
    let labels: Vec<String> = names.into_iter().map(key_name).collect();
    format!("[{}]", json_strings(&labels))
}

pub fn keys_down() -> String {
    if headless() {
        return "[]".to_string();
    }
    key_set(get_keys_down().into_iter().collect())
}

pub fn keys_pressed() -> String {
    if headless() {
        return "[]".to_string();
    }
    key_set(get_keys_pressed().into_iter().collect())
}

pub fn keys_released() -> String {
    if headless() {
        return "[]".to_string();
    }
    key_set(get_keys_released().into_iter().collect())
}

pub fn char_pressed() -> String {
    if headless() {
        return String::new();
    }
    get_char_pressed()
        .map(|c| c.to_string())
        .unwrap_or_default()
}

pub fn mouse() -> String {
    if headless() {
        return with(|state| format!("[{},{}]", number(state.mouse_x), number(state.mouse_y)));
    }
    let (x, y) = mouse_position();
    format!("[{},{}]", number(x), number(y))
}

pub fn mouse_local() -> String {
    if headless() {
        return "[0,0]".to_string();
    }
    let point = mouse_position_local();
    format!("[{},{}]", number(point.x), number(point.y))
}

pub fn mouse_delta() -> String {
    if headless() {
        return "[0,0]".to_string();
    }
    let point = mouse_delta_position();
    format!("[{},{}]", number(point.x), number(point.y))
}

pub fn wheel() -> String {
    if headless() {
        return with(|state| format!("[{},{}]", number(state.scroll_x), number(state.scroll_y)));
    }
    let (x, y) = mouse_wheel();
    format!("[{},{}]", number(x), number(y))
}

/// `0` = left, `1` = middle, `2` = right, anything else = unknown.
fn button_of(index: i64) -> MouseButton {
    match index {
        0 => MouseButton::Left,
        1 => MouseButton::Middle,
        2 => MouseButton::Right,
        _ => MouseButton::Unknown,
    }
}

pub fn mouse_down(index: i64) -> bool {
    !headless() && is_mouse_button_down(button_of(index))
}

pub fn mouse_pressed(index: i64) -> bool {
    !headless() && is_mouse_button_pressed(button_of(index))
}

pub fn mouse_released(index: i64) -> bool {
    !headless() && is_mouse_button_released(button_of(index))
}

fn phase_name(phase: TouchPhase) -> &'static str {
    match phase {
        TouchPhase::Started => "started",
        TouchPhase::Stationary => "stationary",
        TouchPhase::Moved => "moved",
        TouchPhase::Ended => "ended",
        TouchPhase::Cancelled => "cancelled",
    }
}

pub fn touch_list() -> String {
    if headless() {
        return "[]".to_string();
    }
    let entries: Vec<String> = touches()
        .into_iter()
        .map(|touch| {
            format!(
                "{{\"id\":{},\"x\":{},\"y\":{},\"phase\":\"{}\"}}",
                touch.id,
                number(touch.position.x),
                number(touch.position.y),
                phase_name(touch.phase)
            )
        })
        .collect();
    format!("[{}]", entries.join(","))
}

pub fn dropped() -> String {
    if headless() {
        return "[]".to_string();
    }
    let names: Vec<String> = get_dropped_files()
        .into_iter()
        .filter_map(|file| file.path.map(|path| path.to_string_lossy().into_owned()))
        .collect();
    format!("[{}]", json_strings(&names))
}

pub fn clear_queue() {
    if !headless() {
        clear_input_queue();
    }
}

pub fn simulate_touch(value: bool) {
    if !headless() {
        simulate_mouse_with_touch(value);
    }
}

/// JSON string array body (no surrounding brackets), with minimal escaping.
pub fn json_strings(values: &[String]) -> String {
    let quoted: Vec<String> = values
        .iter()
        .map(|value| {
            let escaped = value
                .replace('\\', "\\\\")
                .replace('"', "\\\"")
                .replace('\n', "\\n");
            format!("\"{escaped}\"")
        })
        .collect();
    quoted.join(",")
}
