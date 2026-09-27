//! Additional `ratatui` widgets exposed by the `tui` module.
//!
//! Each function renders offscreen and returns the text for the caller to print,
//! mirroring how `lib.rs` handles paragraphs, tables and trees.

use crate::render::render_to_string;
use ratatui::text::Line;
use ratatui::widgets::{
    Bar, BarChart, BarGroup, Block, Borders, Gauge, LineGauge, List, ListItem, Sparkline, Tabs,
    Widget,
};
use serde_json::Value;

pub fn list(width: u16, ansi: bool, items: &[String], title: &str, numbered: bool) -> String {
    let entries: Vec<ListItem> = items
        .iter()
        .enumerate()
        .map(|(index, item)| {
            let text = if numbered {
                format!("{}. {}", index + 1, item)
            } else {
                format!("• {}", item)
            };
            ListItem::new(text)
        })
        .collect();
    let mut block = Block::default().borders(Borders::ALL);
    if !title.is_empty() {
        block = block.title(title.to_string());
    }
    let height = (entries.len() as u16).saturating_add(2).max(2);
    render_to_string(width, height, ansi, true, move |area, buffer| {
        List::new(entries).block(block).render(area, buffer);
    })
}

pub fn tabs(width: u16, ansi: bool, labels: &[String], active: i64) -> String {
    let titles: Vec<String> = labels.to_vec();
    let selected = if labels.is_empty() {
        0
    } else {
        active.clamp(0, labels.len() as i64 - 1) as usize
    };
    render_to_string(width, 1, ansi, true, move |area, buffer| {
        Tabs::new(titles)
            .select(selected)
            .divider(" ")
            .render(area, buffer);
    })
}

pub fn bar_chart(
    width: u16,
    ansi: bool,
    title: &str,
    data: &[u64],
    requested_width: u16,
    requested_height: u16,
) -> String {
    let values = data.to_vec();
    let render_width = if requested_width == 0 {
        width
    } else {
        requested_width.min(width)
    };
    let render_height = if requested_height == 0 {
        (values.len() as u16).saturating_add(2).max(4)
    } else {
        requested_height.max(3)
    };
    let mut block = Block::default().borders(Borders::ALL);
    if !title.is_empty() {
        block = block.title(title.to_string());
    }
    render_to_string(
        render_width,
        render_height,
        ansi,
        true,
        move |area, buffer| {
            let bars: Vec<Bar> = values
                .iter()
                .enumerate()
                .map(|(index, value)| {
                    Bar::default()
                        .value(*value)
                        .label(Line::from(format!("{index}")))
                })
                .collect();
            let group = BarGroup::default().bars(&bars);
            BarChart::default()
                .data(group)
                .bar_width(3)
                .bar_gap(1)
                .block(block)
                .render(area, buffer);
        },
    )
}

pub fn sparkline(width: u16, ansi: bool, data: &[u64]) -> String {
    let values = data.to_vec();
    let render_width = (values.len().max(1) as u16).min(width.max(1));
    render_to_string(render_width, 1, ansi, true, move |area, buffer| {
        Sparkline::default().data(&values).render(area, buffer);
    })
}

pub fn gauge(width: u16, ansi: bool, label: &str, ratio: f64) -> String {
    let text = label.to_string();
    let value = ratio.clamp(0.0, 1.0);
    render_to_string(width, 1, ansi, true, move |area, buffer| {
        Gauge::default()
            .ratio(value)
            .label(text)
            .render(area, buffer);
    })
}

pub fn line_gauge(width: u16, ansi: bool, label: &str, ratio: f64) -> String {
    let text = label.to_string();
    let value = ratio.clamp(0.0, 1.0);
    render_to_string(width, 1, ansi, true, move |area, buffer| {
        LineGauge::default()
            .ratio(value)
            .label(text)
            .render(area, buffer);
    })
}

/// A month grid. `time`/`chrono` is deliberately not pulled in: a calendar is
/// simple arithmetic and this keeps the dependency set small.
pub fn calendar(year: i64, month: i64) -> String {
    const NAMES: [&str; 12] = [
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December",
    ];
    let year = year.clamp(1, 9999) as i32;
    let month = month.clamp(1, 12) as u32;
    let days = days_in_month(year, month);
    // Sakamoto's algorithm: 0 = Sunday.
    let first_sunday = day_of_week(year, month, 1);
    let first_column = ((first_sunday + 6) % 7) as usize; // Monday-first

    let mut lines = Vec::new();
    lines.push(format!("{} {}", NAMES[(month - 1) as usize], year));
    lines.push("Mo Tu We Th Fr Sa Su".to_string());
    let mut week = vec!["  ".to_string(); first_column];
    for day in 1..=days {
        week.push(format!("{day:>2}"));
        if week.len() == 7 {
            lines.push(week.join(" ").trim_end().to_string());
            week.clear();
        }
    }
    if !week.is_empty() {
        while week.len() < 7 {
            week.push("  ".to_string());
        }
        lines.push(week.join(" ").trim_end().to_string());
    }
    lines.join("\n")
}

fn days_in_month(year: i32, month: u32) -> u32 {
    match month {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 => {
            if (year % 4 == 0 && year % 100 != 0) || year % 400 == 0 {
                29
            } else {
                28
            }
        }
        _ => 30,
    }
}

fn day_of_week(year: i32, month: u32, day: u32) -> u32 {
    const OFFSETS: [i32; 12] = [0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4];
    let mut adjusted = year;
    if month < 3 {
        adjusted -= 1;
    }
    ((adjusted + adjusted / 4 - adjusted / 100
        + adjusted / 400
        + OFFSETS[(month - 1) as usize]
        + day as i32)
        % 7) as u32
}

/// Renders a JSON value as an indented tree.
pub fn json_tree(value: &Value) -> String {
    let mut lines = Vec::new();
    match value {
        Value::Object(map) => {
            lines.push("{".to_string());
            let count = map.len();
            for (index, (key, child)) in map.iter().enumerate() {
                let last = index + 1 == count;
                render_child(child, &format!("\"{key}\""), "", last, &mut lines);
            }
            lines.push("}".to_string());
        }
        Value::Array(items) => {
            lines.push("[".to_string());
            let count = items.len();
            for (index, child) in items.iter().enumerate() {
                let last = index + 1 == count;
                render_child(child, "", "", last, &mut lines);
            }
            lines.push("]".to_string());
        }
        other => lines.push(scalar(other)),
    }
    lines.join("\n")
}

fn render_child(value: &Value, key: &str, prefix: &str, last: bool, lines: &mut Vec<String>) {
    let branch = if last { "└─ " } else { "├─ " };
    let label = if key.is_empty() {
        String::new()
    } else {
        format!("{key}: ")
    };
    match value {
        Value::Object(map) => {
            lines.push(format!("{prefix}{branch}{label}{{"));
            let next_prefix = format!("{prefix}{}", if last { "   " } else { "│  " });
            let count = map.len();
            for (index, (child_key, child)) in map.iter().enumerate() {
                let child_last = index + 1 == count;
                render_child(
                    child,
                    &format!("\"{child_key}\""),
                    &next_prefix,
                    child_last,
                    lines,
                );
            }
            lines.push(format!("{next_prefix}}}"));
        }
        Value::Array(items) => {
            lines.push(format!("{prefix}{branch}{label}["));
            let next_prefix = format!("{prefix}{}", if last { "   " } else { "│  " });
            let count = items.len();
            for (index, child) in items.iter().enumerate() {
                let child_last = index + 1 == count;
                render_child(child, "", &next_prefix, child_last, lines);
            }
            lines.push(format!("{next_prefix}]"));
        }
        other => lines.push(format!("{prefix}{branch}{label}{}", scalar(other))),
    }
}

fn scalar(value: &Value) -> String {
    match value {
        Value::String(text) => format!("\"{text}\""),
        other => other.to_string(),
    }
}

/// Left-aligned column text is enough for a quick CSV preview.
pub fn csv_rows(text: &str) -> Vec<Vec<String>> {
    let mut rows = Vec::new();
    for line in text.lines() {
        if line.trim().is_empty() {
            continue;
        }
        rows.push(parse_csv_line(line));
    }
    rows
}

fn parse_csv_line(line: &str) -> Vec<String> {
    let mut fields = Vec::new();
    let mut field = String::new();
    let mut quoted = false;
    let mut chars = line.chars().peekable();
    while let Some(character) = chars.next() {
        match character {
            '"' if quoted && chars.peek() == Some(&'"') => {
                chars.next();
                field.push('"');
            }
            '"' => quoted = !quoted,
            ',' if !quoted => fields.push(std::mem::take(&mut field)),
            _ => field.push(character),
        }
    }
    fields.push(field);
    fields
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn calendar_grid_is_stable() {
        let text = calendar(2026, 7);
        assert!(text.starts_with("July 2026"));
        assert!(text.contains("Mo Tu We Th Fr Sa Su"));
    }

    #[test]
    fn json_tree_nests() {
        let value: Value = serde_json::from_str("{\"a\":1,\"b\":[2]}").unwrap();
        let text = json_tree(&value);
        assert!(text.contains("\"a\": 1"));
        assert!(text.contains("["));
    }

    #[test]
    fn csv_splits_quoted_fields() {
        let rows = csv_rows("a,\"b,c\"\n1,2");
        assert_eq!(rows[0], vec!["a", "b,c"]);
        assert_eq!(rows[1], vec!["1", "2"]);
    }
}
