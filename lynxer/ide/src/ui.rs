//! Dark, modern chrome for the Lynxer IDE.
//!
//! Keeping the palette and the widget styles in one place makes the layout
//! in `main.rs` purely structural.

use iced::widget::{button, container};
use iced::{Background, Border, Color, Theme};

pub fn surface() -> Color {
    Color::from_rgb8(0x1e, 0x20, 0x25)
}
pub fn panel() -> Color {
    Color::from_rgb8(0x24, 0x27, 0x2d)
}
pub fn editor_bg() -> Color {
    Color::from_rgb8(0x18, 0x1a, 0x1e)
}
pub fn border() -> Color {
    Color::from_rgb8(0x33, 0x37, 0x3f)
}
pub fn accent() -> Color {
    Color::from_rgb8(0x4e, 0xc9, 0xb0)
}
pub fn muted() -> Color {
    Color::from_rgb8(0x89, 0x92, 0x9e)
}
pub fn text() -> Color {
    Color::from_rgb8(0xd8, 0xdc, 0xe2)
}
pub fn status_bg() -> Color {
    Color::from_rgb8(0x0f, 0x45, 0x4f)
}
pub fn error() -> Color {
    Color::from_rgb8(0xf1, 0x4c, 0x4c)
}
pub fn warning() -> Color {
    Color::from_rgb8(0xcc, 0xa7, 0x00)
}
pub fn info() -> Color {
    Color::from_rgb8(0x37, 0x94, 0xff)
}
pub fn popup_bg() -> Color {
    Color::from_rgb8(0x20, 0x23, 0x29)
}

fn hairline() -> Border {
    Border {
        color: border(),
        width: 1.0,
        ..Border::default()
    }
}

fn base() -> container::Style {
    container::Style::default()
}

pub fn top_bar(_: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(surface())),
        border: hairline(),
        ..base()
    }
}

pub fn activity_bar(_: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(editor_bg())),
        border: hairline(),
        ..base()
    }
}

pub fn sidebar(_: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(panel())),
        border: hairline(),
        ..base()
    }
}

pub fn tab_bar(_: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(surface())),
        border: hairline(),
        ..base()
    }
}

pub fn breadcrumb(_: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(editor_bg())),
        border: hairline(),
        ..base()
    }
}

pub fn editor(_: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(editor_bg())),
        ..base()
    }
}

pub fn panel_head(_: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(panel())),
        border: hairline(),
        ..base()
    }
}

pub fn output(_: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(editor_bg())),
        ..base()
    }
}

pub fn status_bar(_: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(status_bg())),
        text_color: Some(text()),
        border: hairline(),
        ..base()
    }
}

pub fn completion_popup(_: &Theme) -> container::Style {
    container::Style {
        background: Some(Background::Color(popup_bg())),
        border: Border {
            color: accent(),
            width: 1.0,
            radius: 6.0.into(),
            ..Border::default()
        },
        ..base()
    }
}

pub fn tab_active(_: &Theme, _: button::Status) -> button::Style {
    button::Style {
        background: Some(Background::Color(editor_bg())),
        text_color: accent(),
        border: Border {
            color: border(),
            width: 1.0,
            radius: 4.0.into(),
            ..Border::default()
        },
        ..button::Style::default()
    }
}

pub fn tab_inactive(_: &Theme, _: button::Status) -> button::Style {
    button::Style {
        background: None,
        text_color: muted(),
        border: Border::default(),
        ..button::Style::default()
    }
}

pub fn panel_tab_active(_: &Theme, _: button::Status) -> button::Style {
    button::Style {
        background: Some(Background::Color(editor_bg())),
        text_color: text(),
        border: Border {
            color: accent(),
            width: 1.0,
            ..Border::default()
        },
        ..button::Style::default()
    }
}

pub fn panel_tab_inactive(_: &Theme, _: button::Status) -> button::Style {
    button::Style {
        background: None,
        text_color: muted(),
        border: Border::default(),
        ..button::Style::default()
    }
}

pub fn completion_selected(_: &Theme, _: button::Status) -> button::Style {
    button::Style {
        background: Some(Background::Color(Color::from_rgb8(0x2d, 0x36, 0x40))),
        text_color: accent(),
        border: Border::default(),
        ..button::Style::default()
    }
}

pub fn completion_idle(_: &Theme, _: button::Status) -> button::Style {
    button::Style {
        background: None,
        text_color: text(),
        border: Border::default(),
        ..button::Style::default()
    }
}

pub fn severity_color(severity: crate::diagnostics::Severity) -> Color {
    match severity {
        crate::diagnostics::Severity::Error => error(),
        crate::diagnostics::Severity::Warning => warning(),
        crate::diagnostics::Severity::Info => info(),
    }
}

/// Style pickers: returning a plain fn pointer keeps the two branches of an
/// `if` type-compatible when passed to `Button::style`.
pub type ButtonStyleFn = fn(&Theme, button::Status) -> button::Style;

pub fn tab_style(active: bool) -> ButtonStyleFn {
    if active { tab_active } else { tab_inactive }
}

pub fn panel_tab_style(active: bool) -> ButtonStyleFn {
    if active { panel_tab_active } else { panel_tab_inactive }
}

pub fn completion_style(selected: bool) -> ButtonStyleFn {
    if selected { completion_selected } else { completion_idle }
}

/// A container style with no background or border (used when the completion
/// popup is hidden, so the always-present Stack layer stays invisible).
pub type ContainerStyleFn = fn(&Theme) -> container::Style;

pub fn transparent(_: &Theme) -> container::Style {
    container::Style::default()
}

pub fn popup_style(visible: bool) -> ContainerStyleFn {
    if visible { completion_popup } else { transparent }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hidden_popup_style_is_invisible() {
        let hidden = transparent(&Theme::Dark);
        assert!(hidden.background.is_none());
        assert_eq!(hidden.border.width, 0.0);
        assert!(completion_popup(&Theme::Dark).background.is_some());
        // The always-present layer must pick the invisible style when hidden.
        assert!(popup_style(false)(&Theme::Dark).background.is_none());
        assert!(popup_style(true)(&Theme::Dark).background.is_some());
    }
}
