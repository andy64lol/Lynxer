//! Symbolic SVG icons and the official logo for the IDE chrome.
//!
//! The SVGs live in `assets/icons/` and are embedded with `include_bytes!`,
//! so the binary has no runtime dependency on asset paths. Every icon is a
//! monochrome alpha mask: iced's `svg::Style.color` filter replaces the RGB
//! channels with the tint we choose at call time.
//!
//! `assets/lynxer.png` is a copy of the repository root `assets/lynxer.png` —
//! the official Lynxer logo (teal hexagon with an "L"). It is embedded as raw
//! PNG bytes and used as the window icon and the in-app brand mark.

use iced::widget::svg::{Handle, Status, Style, Svg};
use iced::Color;

/// The official Lynxer logo, copied from the repo root `assets/lynxer.png`.
pub const LOGO_PNG: &[u8] = include_bytes!("../assets/lynxer.png");

pub const FOLDER: &[u8] = include_bytes!("../assets/icons/folder.svg");
pub const FILE: &[u8] = include_bytes!("../assets/icons/file.svg");
pub const FILE_PLUS: &[u8] = include_bytes!("../assets/icons/file-plus.svg");
pub const SAVE: &[u8] = include_bytes!("../assets/icons/save.svg");
pub const PLAY: &[u8] = include_bytes!("../assets/icons/play.svg");
pub const CHECK_CIRCLE: &[u8] = include_bytes!("../assets/icons/check-circle.svg");
pub const BRACES: &[u8] = include_bytes!("../assets/icons/braces.svg");
pub const SLIDERS: &[u8] = include_bytes!("../assets/icons/sliders.svg");
pub const REFRESH: &[u8] = include_bytes!("../assets/icons/refresh.svg");
pub const X: &[u8] = include_bytes!("../assets/icons/x.svg");
pub const CHEVRON_RIGHT: &[u8] = include_bytes!("../assets/icons/chevron-right.svg");
pub const CHEVRON_DOWN: &[u8] = include_bytes!("../assets/icons/chevron-down.svg");
pub const TERMINAL: &[u8] = include_bytes!("../assets/icons/terminal.svg");
pub const ALERT_TRIANGLE: &[u8] = include_bytes!("../assets/icons/alert-triangle.svg");
pub const TRASH: &[u8] = include_bytes!("../assets/icons/trash.svg");
pub const PANEL: &[u8] = include_bytes!("../assets/icons/panel.svg");

#[cfg(test)]
const ALL: &[&[u8]] = &[FOLDER, FILE, FILE_PLUS, SAVE, PLAY, CHECK_CIRCLE, BRACES, SLIDERS, REFRESH, X, CHEVRON_RIGHT, CHEVRON_DOWN, TERMINAL, ALERT_TRIANGLE, TRASH, PANEL];

/// A tinted symbolic icon rendered at `size` pixels.
pub fn icon<'a>(bytes: &'static [u8], color: Color, size: f32) -> Svg<'a> {
    Svg::new(Handle::from_memory(bytes))
        .width(size)
        .height(size)
        .style(move |_theme: &iced::Theme, _status: Status| Style {
            color: Some(color),
        })
}

/// The official Lynxer logo rendered at `size` pixels.
pub fn logo(size: f32) -> iced::widget::Image {
    iced::widget::image(iced::advanced::image::Handle::from_bytes(LOGO_PNG))
        .width(size)
        .height(size)
}

/// The window/taskbar icon, decoded from the embedded PNG at startup. Returns
/// `None` rather than failing the app if the bytes cannot be decoded.
pub fn window_icon() -> Option<iced::window::Icon> {
    let decoded = ::image::load_from_memory(LOGO_PNG).ok()?.into_rgba8();
    let (width, height) = decoded.dimensions();
    iced::window::icon::from_rgba(decoded.into_raw(), width, height).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_icon_is_a_well_formed_symbolic_svg() {
        assert_eq!(ALL.len(), 16);
        for bytes in ALL {
            let source = std::str::from_utf8(bytes).unwrap();
            assert!(source.starts_with("<svg"), "{source}");
            assert!(source.contains("xmlns=\"http://www.w3.org/2000/svg\""));
            assert!(source.contains("viewBox=\"0 0 24 24\""));
            assert!(source.trim_end().ends_with("</svg>"));
            assert!(source.contains("stroke=\"#ffffff\"") || source.contains("fill=\"#ffffff\""));
        }
    }

    #[test]
    fn embedded_logo_is_the_official_png() {
        assert!(LOGO_PNG.starts_with(&[0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a]));
        // IHDR width and height are big-endian u32 at bytes 16..24.
        let width = u32::from_be_bytes(LOGO_PNG[16..20].try_into().unwrap());
        let height = u32::from_be_bytes(LOGO_PNG[20..24].try_into().unwrap());
        assert_eq!((width, height), (48, 48));
    }

    #[test]
    fn window_icon_decodes() {
        assert!(window_icon().is_some());
    }
}
