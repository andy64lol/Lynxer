//! A small style-string parser for the `tui` module.
//!
//! Lynxer call sites pass styles as strings such as `"bold green"` or
//! `"white on blue"`, matching the subset of Rich's style syntax the stdlib
//! documents. This turns one into a `ratatui::style::Style` and can render it
//! back to an ANSI SGR sequence for terminal output.

use ratatui::style::{Color, Modifier, Style};
use std::str::FromStr;
use unicode_width::UnicodeWidthChar;

/// Parses a style string. `None` means the string is not a valid style, which
/// is what makes `styleValid` reject unknown tokens.
pub fn parse_style(text: &str) -> Option<Style> {
    let trimmed = text.trim();
    if trimmed.is_empty() {
        return Some(Style::default());
    }

    let mut style = Style::default();
    let mut saw_token = false;
    let mut background = false;
    for token in trimmed.split_whitespace() {
        if token == "on" {
            background = true;
            continue;
        }
        if let Some(modifier) = parse_modifier(token) {
            style = style.add_modifier(modifier);
            saw_token = true;
            continue;
        }
        match Color::from_str(token) {
            Ok(color) => {
                style = if background {
                    style.bg(color)
                } else {
                    style.fg(color)
                };
                background = false;
                saw_token = true;
            }
            Err(_) => return None,
        }
    }
    if saw_token {
        Some(style)
    } else {
        None
    }
}

fn parse_modifier(token: &str) -> Option<Modifier> {
    match token {
        "bold" => Some(Modifier::BOLD),
        "dim" => Some(Modifier::DIM),
        "italic" => Some(Modifier::ITALIC),
        "underline" | "underlined" => Some(Modifier::UNDERLINED),
        "blink" => Some(Modifier::SLOW_BLINK),
        "reverse" | "reversed" => Some(Modifier::REVERSED),
        "hidden" => Some(Modifier::HIDDEN),
        "strike" | "strikethrough" | "crossedout" => Some(Modifier::CROSSED_OUT),
        _ => None,
    }
}

/// Renders a style as the parameters of an ANSI SGR sequence (`CSI <params> m`).
/// Returns an empty string when the style carries nothing to emit.
pub fn style_to_sgr(style: Style) -> String {
    let mut params: Vec<String> = Vec::new();
    if let Some(color) = style.fg {
        if let Some(param) = color_param(color, false) {
            params.push(param);
        }
    }
    if let Some(color) = style.bg {
        if let Some(param) = color_param(color, true) {
            params.push(param);
        }
    }
    let modifier = style.add_modifier;
    if modifier.contains(Modifier::BOLD) {
        params.push("1".to_string());
    }
    if modifier.contains(Modifier::DIM) {
        params.push("2".to_string());
    }
    if modifier.contains(Modifier::ITALIC) {
        params.push("3".to_string());
    }
    if modifier.contains(Modifier::UNDERLINED) {
        params.push("4".to_string());
    }
    if modifier.contains(Modifier::SLOW_BLINK) {
        params.push("5".to_string());
    }
    if modifier.contains(Modifier::RAPID_BLINK) {
        params.push("6".to_string());
    }
    if modifier.contains(Modifier::REVERSED) {
        params.push("7".to_string());
    }
    if modifier.contains(Modifier::HIDDEN) {
        params.push("8".to_string());
    }
    if modifier.contains(Modifier::CROSSED_OUT) {
        params.push("9".to_string());
    }
    params.join(";")
}

fn color_param(color: Color, background: bool) -> Option<String> {
    let (base, bright) = if background { (40, 100) } else { (30, 90) };
    let code = match color {
        Color::Reset => return None,
        Color::Black => base,
        Color::Red => base + 1,
        Color::Green => base + 2,
        Color::Yellow => base + 3,
        Color::Blue => base + 4,
        Color::Magenta => base + 5,
        Color::Cyan => base + 6,
        Color::Gray => base + 7,
        Color::DarkGray => bright,
        Color::LightRed => bright + 1,
        Color::LightGreen => bright + 2,
        Color::LightYellow => bright + 3,
        Color::LightBlue => bright + 4,
        Color::LightMagenta => bright + 5,
        Color::LightCyan => bright + 6,
        Color::White => bright + 7,
        Color::Rgb(r, g, b) => {
            let selector = if background { 48 } else { 38 };
            return Some(format!("{selector};2;{r};{g};{b}"));
        }
        Color::Indexed(index) => {
            let selector = if background { 48 } else { 38 };
            return Some(format!("{selector};5;{index}"));
        }
    };
    Some(code.to_string())
}

/// The display width of a string, used for wrapping and alignment.
pub fn text_width(text: &str) -> usize {
    text.chars()
        .map(|character| UnicodeWidthChar::width(character).unwrap_or(0))
        .sum()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_colors_and_modifiers() {
        assert!(parse_style("bold green").is_some());
        assert!(parse_style("white on blue").is_some());
        assert!(parse_style("not-a-style").is_none());
        assert!(parse_style("").is_some());
    }

    #[test]
    fn measures_width() {
        assert_eq!(text_width("abc"), 3);
    }
}
