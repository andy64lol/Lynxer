//! Offscreen rendering helpers.
//!
//! The `tui` module never opens a terminal: it draws a widget into a
//! `ratatui::buffer::Buffer` and turns that buffer back into a string. Plain
//! text is the default so runs without a TTY (CI, fixtures) are deterministic;
//! ANSI SGR sequences are added only when the caller says the terminal supports
//! them.

use crate::style::text_width;
use ratatui::buffer::Buffer;
use ratatui::layout::Rect;

/// An upper bound on the number of rows `text` occupies when wrapped to
/// `width`. Over-estimating is safe because [`buffer_to_string`] drops trailing
/// blank rows; under-estimating would clip the widget.
pub fn wrapped_height(text: &str, width: usize, soft_wrap: bool) -> u16 {
    let usable = width.max(1);
    let mut rows = 0usize;
    for line in text.split('\n') {
        let line_width = text_width(line);
        let count = if soft_wrap && line_width > usable {
            line_width.div_ceil(usable)
        } else {
            1
        };
        rows += count.max(1);
    }
    rows.max(1) as u16
}

/// Renders a widget into a fresh buffer of the given size and converts it to a
/// string. `draw` receives the area and the buffer to render into.
pub fn render_to_string(
    width: u16,
    height: u16,
    ansi: bool,
    trim: bool,
    draw: impl FnOnce(Rect, &mut Buffer),
) -> String {
    let area = Rect::new(0, 0, width.max(1), height.max(1));
    let mut buffer = Buffer::empty(area);
    draw(area, &mut buffer);
    buffer_to_string(&buffer, ansi, trim)
}

/// Converts a buffer to text, one row per line.
///
/// Wide-character continuation cells are skipped, each row is right-trimmed,
/// and (when `trim`) trailing blank rows are dropped. When `ansi` is set, runs
/// of equal style are wrapped in SGR sequences.
pub fn buffer_to_string(buffer: &Buffer, ansi: bool, trim: bool) -> String {
    let area = buffer.area();
    let mut rows: Vec<String> = Vec::with_capacity(area.height as usize);
    for y in 0..area.height {
        let mut row = String::new();
        if ansi {
            let mut current: Option<ratatui::style::Style> = None;
            for x in 0..area.width {
                let cell = buffer.get(x, y);
                if cell.skip {
                    continue;
                }
                let style = cell.style();
                if current != Some(style) {
                    if current.is_some() {
                        row.push_str("\x1b[0m");
                    }
                    let params = crate::style::style_to_sgr(style);
                    if !params.is_empty() {
                        row.push_str("\x1b[");
                        row.push_str(&params);
                        row.push('m');
                    }
                    current = Some(style);
                }
                row.push_str(&cell.symbol);
            }
            if current.is_some() {
                row.push_str("\x1b[0m");
            }
        } else {
            for x in 0..area.width {
                let cell = buffer.get(x, y);
                if cell.skip {
                    continue;
                }
                row.push_str(&cell.symbol);
            }
        }
        rows.push(row.trim_end().to_string());
    }

    if trim {
        while rows.last().is_some_and(|row| row.trim().is_empty()) {
            rows.pop();
        }
    }
    rows.join("\n")
}

/// Pads `text` to `width` on each line with `left`/`right` spaces and adds
/// `top`/`bottom` blank lines. Used by `printPadded`, where trailing blanks are
/// meaningful and must not be trimmed.
pub fn pad(
    text: &str,
    width: usize,
    top: usize,
    right: usize,
    bottom: usize,
    left: usize,
) -> String {
    let inner = width.saturating_sub(left + right).max(1);
    let mut rows: Vec<String> = Vec::new();
    for _ in 0..top {
        rows.push(String::new());
    }
    for line in text.split('\n') {
        let mut rendered = " ".repeat(left);
        rendered.push_str(line);
        let used = left + text_width(line);
        let target = inner + left;
        if used < target {
            rendered.push_str(&" ".repeat(target - used));
        }
        if right > 0 {
            rendered.push_str(&" ".repeat(right));
        }
        rows.push(rendered.trim_end().to_string());
    }
    for _ in 0..bottom {
        rows.push(String::new());
    }
    rows.join("\n")
}
