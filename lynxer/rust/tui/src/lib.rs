//! Lynxer `tui` stdlib backend: a terminal UI built on `ratatui`.
//!
//! The Lynxer-facing contract matches `lynxer/stdlib/tui.lynx`: structured data
//! crosses as JSON strings and failures as `"Error: <message>"`.
//!
//! Rendering never opens a terminal: each operation draws a `ratatui` widget
//! into an offscreen buffer and prints the result. Without a TTY (CI, fixture
//! runs) the output is plain text, so it is deterministic; with a TTY and color
//! enabled it carries ANSI SGR styling. Prompts read stdin and fall back to a
//! documented default at end-of-file, so a non-interactive run never blocks.
//!
//! The stateful families (`table*`, `tree*`, `layout*`, `progress*`,
//! `status*`, `live*`) keep real state in the backend, addressed by integer
//! handles.

mod markdown;
mod prompt;
mod render;
mod style;
mod syntax;
mod widgets_ext;

use crate::prompt::{parse_confirm, parse_float, parse_int, read_line};
use crate::render::{pad, render_to_string, wrapped_height};
use crate::style::{parse_style, style_to_sgr, text_width};
use crate::syntax::Highlighter;

use crossterm::event::{self, Event as TermEvent, KeyCode};
use crossterm::tty::IsTty;
use lynxer_abi::{export_float, export_int, export_string, lynxer_module};
use ratatui::layout::{Alignment, Constraint, Direction, Layout};
use ratatui::style::Style;
use ratatui::text::{Line, Span};
use ratatui::widgets::{
    Block, Borders, Cell as TableCell, Gauge, Paragraph, Row, Table, Widget, Wrap,
};
use serde_json::Value;
use std::io::{BufRead, Write};
use std::sync::Mutex;

// --- State -----------------------------------------------------------------

struct Column {
    header: String,
    style: Style,
}

struct TableState {
    title: String,
    columns: Vec<Column>,
    rows: Vec<Vec<String>>,
    caption: String,
    show_header: bool,
    show_lines: bool,
    boxed: bool,
    expand: bool,
}

struct Node {
    label: String,
    children: Vec<usize>,
}

struct Section {
    name: String,
    text: String,
    title: String,
    panel: bool,
}

struct LayoutState {
    rows: bool,
    sections: Vec<Section>,
}

#[derive(Clone)]
struct Task {
    description: String,
    total: f64,
    completed: f64,
}

#[derive(Clone)]
struct ProgressState {
    tasks: Vec<Task>,
}

struct LiveState {
    text: String,
    title: String,
    panel: bool,
}

struct TuiState {
    width: u16,
    color: Option<bool>,
    markup: bool,
    emoji: bool,
    highlight: bool,
    soft_wrap: bool,
    lines: Vec<String>,
    raw_mode: bool,
    alt_screen: bool,
    tables: Vec<Option<TableState>>,
    trees: Vec<Option<Node>>,
    layouts: Vec<Option<LayoutState>>,
    progress: Vec<Option<ProgressState>>,
    status: Vec<Option<String>>,
    live: Vec<Option<LiveState>>,
    screen: Option<String>,
    highlighter: Option<Box<Highlighter>>,
}

impl TuiState {
    fn new() -> Self {
        TuiState {
            width: 80,
            color: None,
            markup: true,
            emoji: true,
            highlight: true,
            soft_wrap: true,
            lines: Vec::new(),
            raw_mode: false,
            alt_screen: false,
            tables: Vec::new(),
            trees: Vec::new(),
            layouts: Vec::new(),
            progress: Vec::new(),
            status: Vec::new(),
            live: Vec::new(),
            screen: None,
            highlighter: None,
        }
    }

    /// Loads the syntax and theme sets on first use (they are large).
    fn highlighter(&mut self) -> &mut Highlighter {
        if self.highlighter.is_none() {
            self.highlighter = Some(Box::new(Highlighter::new()));
        }
        self.highlighter.as_mut().unwrap()
    }
}

thread_local! {
    static STATE: Mutex<Option<TuiState>> = Mutex::new(None);
}

fn with_state<F: FnOnce(&mut TuiState) -> R, R>(f: F) -> R {
    STATE.with(|cell| {
        let mut guard = cell.lock().unwrap();
        if guard.is_none() {
            *guard = Some(TuiState::new());
        }
        f(guard.as_mut().unwrap())
    })
}

fn allocate<T>(slots: &mut Vec<Option<T>>, value: T) -> i64 {
    if let Some(index) = slots.iter().position(|slot| slot.is_none()) {
        slots[index] = Some(value);
        index as i64
    } else {
        slots.push(Some(value));
        (slots.len() - 1) as i64
    }
}

fn is_tty() -> bool {
    std::io::stdout().is_tty()
}

fn ansi_enabled(state: &TuiState) -> bool {
    state.color.unwrap_or(true) && is_tty()
}

/// Prints a rendered block and records it for `consoleSaveText`/`Html`.
fn emit(state: &mut TuiState, rendered: &str) {
    println!("{}", rendered);
    let _ = std::io::stdout().flush();
    state.lines.push(rendered.to_string());
}

// --- Rendering helpers -----------------------------------------------------

fn render_paragraph(
    state: &TuiState,
    text: &str,
    style: Style,
    alignment: Alignment,
    block: Option<Block<'static>>,
) -> String {
    let bordered = block.is_some();
    let inner = (state.width as usize)
        .saturating_sub(if bordered { 2 } else { 0 })
        .max(1);
    let mut height = wrapped_height(text, inner, state.soft_wrap);
    if bordered {
        height = height.saturating_add(2);
    }
    let ansi = ansi_enabled(state);
    let wrap = state.soft_wrap;
    let owned = text.to_string();
    render_to_string(state.width, height, ansi, true, move |area, buffer| {
        let mut widget = Paragraph::new(owned).style(style).alignment(alignment);
        if wrap {
            widget = widget.wrap(Wrap { trim: false });
        }
        if let Some(block) = block {
            widget = widget.block(block);
        }
        widget.render(area, buffer);
    })
}

fn panel_block(title: &str, border_style: Style) -> Block<'static> {
    let mut block = Block::default()
        .borders(Borders::ALL)
        .border_style(border_style);
    if !title.is_empty() {
        block = block.title(title.to_string());
    }
    block
}

fn render_rule(width: usize, title: &str, styled: Style, ansi: bool) -> String {
    let line = if title.is_empty() {
        "─".repeat(width)
    } else {
        let label = format!(" {} ", title);
        let label_width = text_width(&label);
        if label_width >= width {
            label.trim().to_string()
        } else {
            let remaining = width - label_width;
            let left = remaining / 2;
            format!(
                "{}{}{}",
                "─".repeat(left),
                label,
                "─".repeat(remaining - left)
            )
        }
    };
    if ansi {
        let params = style_to_sgr(styled);
        if !params.is_empty() {
            return format!("\x1b[{params}m{line}\x1b[0m");
        }
    }
    line
}

fn render_table(state: &TuiState, spec: &TableState) -> String {
    let ansi = ansi_enabled(state);
    let columns = spec.columns.len();
    let mut widths: Vec<usize> = vec![1; columns.max(1)];
    for (index, column) in spec.columns.iter().enumerate() {
        widths[index] = widths[index].max(text_width(&column.header));
    }
    for row in &spec.rows {
        for (index, value) in row.iter().enumerate() {
            if index < widths.len() {
                widths[index] = widths[index].max(text_width(value));
            }
        }
    }

    let header = if spec.show_header && !spec.columns.is_empty() {
        Some(Row::new(
            spec.columns
                .iter()
                .map(|column| TableCell::from(column.header.clone()).style(column.style))
                .collect::<Vec<_>>(),
        ))
    } else {
        None
    };
    let rows: Vec<Row> = spec
        .rows
        .iter()
        .map(|row| {
            Row::new(
                row.iter()
                    .map(|value| TableCell::from(value.clone()))
                    .collect::<Vec<_>>(),
            )
        })
        .collect();

    let constraints: Vec<Constraint> = if spec.expand && !widths.is_empty() {
        vec![Constraint::Ratio(1, widths.len() as u32); widths.len()]
    } else {
        widths
            .iter()
            .map(|width| Constraint::Length((*width + 1).min(state.width as usize) as u16))
            .collect()
    };

    let mut block = if spec.boxed {
        Block::default().borders(Borders::ALL)
    } else {
        Block::default().borders(Borders::NONE)
    };
    let title = if !spec.caption.is_empty() {
        spec.caption.clone()
    } else {
        spec.title.clone()
    };
    if !title.is_empty() {
        block = block.title(title);
    }

    let row_count = rows.len() + usize::from(header.is_some());
    let mut height = row_count.max(1) as u16;
    if spec.boxed {
        height = height.saturating_add(2);
    }
    // ratatui's `Table` has no row separators; `showLines` widens the gap
    // between columns to keep the flag observable.
    let spacing = if spec.show_lines { 1 } else { 2 };
    let owned_rows = rows;
    let owned_constraints = constraints;
    render_to_string(state.width, height, ansi, true, move |area, buffer| {
        let mut table = Table::new(owned_rows)
            .widths(&owned_constraints)
            .column_spacing(spacing)
            .block(block);
        if let Some(header) = header {
            table = table.header(header);
        }
        table.render(area, buffer);
    })
}

fn render_tree(nodes: &[Option<Node>], root: usize) -> String {
    fn walk(nodes: &[Option<Node>], index: usize, prefix: &str, last: bool, out: &mut Vec<String>) {
        let Some(node) = nodes.get(index).and_then(|slot| slot.as_ref()) else {
            return;
        };
        if prefix.is_empty() {
            out.push(node.label.clone());
        } else {
            out.push(format!(
                "{prefix}{}{}",
                if last { "└─ " } else { "├─ " },
                node.label
            ));
        }
        let child_prefix = if prefix.is_empty() {
            String::new()
        } else {
            format!("{prefix}{}", if last { "   " } else { "│  " })
        };
        let children = node.children.clone();
        let count = children.len();
        for (position, child) in children.into_iter().enumerate() {
            walk(nodes, child, &child_prefix, position + 1 == count, out);
        }
    }
    let mut lines = Vec::new();
    walk(nodes, root, "", true, &mut lines);
    lines.join("\n")
}

fn render_layout(state: &TuiState, spec: &LayoutState) -> String {
    if spec.sections.is_empty() {
        return String::new();
    }
    let sections: Vec<(String, String, bool)> = spec
        .sections
        .iter()
        .map(|section| (section.text.clone(), section.title.clone(), section.panel))
        .collect();
    let heights: Vec<u16> = sections
        .iter()
        .map(|(text, _, panel)| {
            let mut height = wrapped_height(text, state.width as usize, state.soft_wrap);
            if *panel {
                height = height.saturating_add(2);
            }
            height
        })
        .collect();
    let height = if spec.rows {
        heights.iter().copied().sum::<u16>().max(1)
    } else {
        heights.iter().copied().max().unwrap_or(1)
    };
    let constraints: Vec<Constraint> = if spec.rows {
        heights
            .iter()
            .map(|height| Constraint::Length(*height))
            .collect()
    } else {
        vec![Constraint::Ratio(1, sections.len() as u32); sections.len()]
    };
    let direction = if spec.rows {
        Direction::Vertical
    } else {
        Direction::Horizontal
    };
    let ansi = ansi_enabled(state);
    let wrap = state.soft_wrap;
    render_to_string(state.width, height, ansi, true, move |area, buffer| {
        let chunks = Layout::default()
            .direction(direction)
            .constraints(constraints)
            .split(area);
        for (index, (text, title, panel)) in sections.iter().enumerate() {
            let Some(chunk) = chunks.get(index) else {
                break;
            };
            let mut widget = Paragraph::new(text.clone());
            if *panel {
                widget = widget.block(panel_block(title, Style::default()));
            }
            if wrap {
                widget = widget.wrap(Wrap { trim: false });
            }
            widget.render(*chunk, buffer);
        }
    })
}

fn render_progress(width: u16, ansi: bool, spec: &ProgressState) -> String {
    if spec.tasks.is_empty() {
        return String::new();
    }
    let tasks = spec.tasks.clone();
    let height = tasks.len() as u16;
    render_to_string(width, height, ansi, true, move |area, buffer| {
        let constraints: Vec<Constraint> = vec![Constraint::Length(1); tasks.len()];
        let chunks = Layout::default()
            .direction(Direction::Vertical)
            .constraints(constraints)
            .split(area);
        for (index, task) in tasks.iter().enumerate() {
            let Some(chunk) = chunks.get(index) else {
                break;
            };
            let ratio = if task.total > 0.0 {
                (task.completed / task.total).clamp(0.0, 1.0)
            } else {
                0.0
            };
            let label = format!(
                "{} {:.0}/{:.0}",
                task.description, task.completed, task.total
            );
            Gauge::default()
                .ratio(ratio)
                .label(label)
                .render(*chunk, buffer);
        }
    })
}

fn json_pretty(text: &str) -> String {
    match serde_json::from_str::<Value>(text) {
        Ok(value) => serde_json::to_string_pretty(&value).unwrap_or_else(|_| text.to_string()),
        Err(_) => text.to_string(),
    }
}

fn markup_spans(text: &str, base: Style) -> Vec<Span<'static>> {
    let mut spans: Vec<Span<'static>> = Vec::new();
    let mut buffer = String::new();
    let mut stack: Vec<Style> = vec![base];
    let mut chars = text.chars().peekable();
    while let Some(character) = chars.next() {
        if character != '[' {
            buffer.push(character);
            continue;
        }
        let mut tag = String::new();
        let mut closed = false;
        while let Some(next) = chars.next() {
            if next == ']' {
                closed = true;
                break;
            }
            tag.push(next);
        }
        if !closed {
            buffer.push('[');
            buffer.push_str(&tag);
            continue;
        }
        if !buffer.is_empty() {
            spans.push(Span::styled(
                std::mem::take(&mut buffer),
                *stack.last().unwrap(),
            ));
        }
        if tag.starts_with('/') {
            if stack.len() > 1 {
                stack.pop();
            }
        } else if let Some(style) = parse_style(&tag) {
            let merged = stack.last().unwrap().patch(style);
            stack.push(merged);
        } else {
            buffer.push('[');
            buffer.push_str(&tag);
            buffer.push(']');
        }
    }
    if !buffer.is_empty() {
        spans.push(Span::styled(buffer, *stack.last().unwrap()));
    }
    if spans.is_empty() {
        spans.push(Span::raw(""));
    }
    spans
}

fn render_lines(state: &TuiState, lines: Vec<Line<'static>>, source: &str) -> String {
    let count = lines.len() as u16;
    let height = wrapped_height(source, state.width as usize, state.soft_wrap).max(count);
    let ansi = ansi_enabled(state);
    let wrap = state.soft_wrap;
    render_to_string(state.width, height, ansi, true, move |area, buffer| {
        let mut widget = Paragraph::new(lines);
        if wrap {
            widget = widget.wrap(Wrap { trim: false });
        }
        widget.render(area, buffer);
    })
}

fn render_columns(state: &TuiState, items: &[String], equal: bool, expand: bool) -> String {
    if items.is_empty() {
        return String::new();
    }
    let widest = items.iter().map(|item| text_width(item)).max().unwrap_or(0);
    let column_width = if equal {
        widest + 2
    } else {
        items
            .iter()
            .map(|item| text_width(item) + 2)
            .max()
            .unwrap_or(2)
    }
    .max(1);
    let total = state.width.max(1) as usize;
    let count = (total / column_width).max(1);
    let mut lines = Vec::new();
    for chunk in items.chunks(count) {
        let mut row = String::new();
        for (index, item) in chunk.iter().enumerate() {
            let last = index + 1 == chunk.len();
            if last {
                row.push_str(item);
            } else if expand || equal {
                row.push_str(item);
                let used = text_width(item);
                if used < column_width {
                    row.push_str(&" ".repeat(column_width - used));
                }
            } else {
                row.push_str(item);
                row.push_str("  ");
            }
        }
        lines.push(row.trim_end().to_string());
    }
    lines.join("\n")
}

fn render_aligned(width: usize, text: &str, align: &str, pad: bool) -> String {
    let mut lines = Vec::new();
    for line in text.split('\n') {
        if !pad {
            lines.push(line.to_string());
            continue;
        }
        let used = text_width(line);
        let gap = width.saturating_sub(used);
        let (left, right) = match align {
            "center" => (gap / 2, gap - gap / 2),
            "right" => (gap, 0),
            _ => (0, gap),
        };
        lines.push(format!("{}{}{}", " ".repeat(left), line, " ".repeat(right)));
    }
    lines.join("\n")
}

fn escape_html(text: &str) -> String {
    text.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
}

// --- Ops -------------------------------------------------------------------

// Returns 1 if ratatui/crossterm is available, 0 otherwise.
export_int!(tui_exists, args, { 1 });

// Returns the backend version string, or "" if unavailable.
export_string!(tui_version, args, { env!("CARGO_PKG_VERSION").to_string() });

// Print text with markup support. Returns 0 on success.
export_int!(tui_print_text, args, {
    let text = args.string(0);
    with_state(|state| {
        let rendered = if state.markup {
            let spans = markup_spans(text, Style::default());
            render_lines(state, vec![Line::from(spans)], text)
        } else {
            render_paragraph(state, text, Style::default(), Alignment::Left, None)
        };
        emit(state, &rendered);
    });
    0
});

// Print text with a Rich style. Returns 0 on success.
export_int!(tui_print_styled, args, {
    let text = args.string(0);
    let style = args.string(1);
    with_state(|state| {
        let parsed = parse_style(style).unwrap_or_default();
        let rendered = render_paragraph(state, text, parsed, Alignment::Left, None);
        emit(state, &rendered);
    });
    0
});

// Render Markdown to the terminal. Returns 0 on success.
export_int!(tui_markdown, args, {
    let text = args.string(0);
    with_state(|state| {
        let highlight = state.highlight;
        let lines = {
            let highlighter = state.highlighter();
            markdown::render(text, highlighter, highlight)
        };
        let rendered = render_lines(state, lines, text);
        emit(state, &rendered);
    });
    0
});

// Render a bordered panel. Returns 0 on success.
export_int!(tui_panel, args, {
    let text = args.string(0);
    let title = args.string(1);
    with_state(|state| {
        let block = panel_block(title, Style::default());
        let rendered =
            render_paragraph(state, text, Style::default(), Alignment::Left, Some(block));
        emit(state, &rendered);
    });
    0
});

// Render a panel with explicit styles. Returns 0 on success.
export_int!(tui_panel_styled, args, {
    let text = args.string(0);
    let title = args.string(1);
    let border_style = args.string(2);
    let content_style = args.string(3);
    with_state(|state| {
        let border = parse_style(border_style).unwrap_or_default();
        let content = parse_style(content_style).unwrap_or_default();
        let block = panel_block(title, border);
        let rendered = render_paragraph(state, text, content, Alignment::Left, Some(block));
        emit(state, &rendered);
    });
    0
});

// Print a horizontal rule. Returns 0 on success.
export_int!(tui_rule, args, {
    let title = args.string(0);
    with_state(|state| {
        let width = state.width as usize;
        let ansi = ansi_enabled(state);
        let rendered = render_rule(width, title, Style::default(), ansi);
        emit(state, &rendered);
    });
    0
});

// Styled rule. Returns 0 on success.
export_int!(tui_rule_styled, args, {
    let title = args.string(0);
    let style = args.string(1);
    with_state(|state| {
        let parsed = parse_style(style).unwrap_or_default();
        let width = state.width as usize;
        let ansi = ansi_enabled(state);
        let rendered = render_rule(width, title, parsed, ansi);
        emit(state, &rendered);
    });
    0
});

// Pretty-print JSON with syntax highlighting. Returns 0 on success.
export_int!(tui_json_pretty, args, {
    let json_text = args.string(0);
    with_state(|state| {
        let rendered = render_paragraph(
            state,
            &json_pretty(json_text),
            Style::default(),
            Alignment::Left,
            None,
        );
        emit(state, &rendered);
    });
    0
});

// Syntax-highlight source code. Returns 0 on success.
export_int!(tui_print_syntax, args, {
    let code = args.string(0);
    let lexer = args.string(1);
    let line_numbers = args.int(0);
    with_state(|state| {
        let lines = if state.highlight {
            let highlighter = state.highlighter();
            highlighter.block_lines(code, lexer, line_numbers != 0)
        } else {
            code.split('\n')
                .map(|line| Line::from(line.to_string()))
                .collect()
        };
        let rendered = render_lines(state, lines, code);
        emit(state, &rendered);
    });
    0
});

// Render a table. columnsJson is a JSON array of column names;
// rowsJson is a JSON array of arrays. Returns 0 on success.
export_int!(tui_table, args, {
    let title = args.string(0);
    let columns_json = args.string(1);
    let rows_json = args.string(2);
    with_state(|state| {
        let mut spec = TableState {
            title: title.to_string(),
            columns: Vec::new(),
            rows: Vec::new(),
            caption: String::new(),
            show_header: true,
            show_lines: false,
            boxed: true,
            expand: false,
        };
        for header in json_string_array(columns_json) {
            spec.columns.push(Column {
                header,
                style: Style::default(),
            });
        }
        spec.rows = json_rows(rows_json);
        let rendered = render_table(state, &spec);
        emit(state, &rendered);
    });
    0
});

// Clear the terminal. Returns 0 on success.
export_int!(tui_clear, args, {
    // The sequence has no trailing newline, so flush explicitly: stdout is
    // line-buffered and these bytes must precede whatever the interpreter
    // prints next.
    print!("\x1B[2J\x1B[1;1H");
    let _ = std::io::stdout().flush();
    0
});

// Read a line of input. Returns the input string, or "" on error.
export_string!(tui_ask, args, {
    let prompt = args.string(0);
    let _ = prompt;
    read_line("")
});

// Enter TUI mode. Returns 0 on success.
export_int!(tui_enter, args, {
    with_state(|state| {
        if is_tty() {
            let _ = crossterm::terminal::enable_raw_mode();
            let _ =
                crossterm::execute!(std::io::stdout(), crossterm::terminal::EnterAlternateScreen);
            state.raw_mode = true;
            state.alt_screen = true;
        }
    });
    0
});

// Exit TUI mode. Returns 0 on success.
export_int!(tui_exit, args, {
    with_state(|state| {
        if state.alt_screen {
            let _ =
                crossterm::execute!(std::io::stdout(), crossterm::terminal::LeaveAlternateScreen);
        }
        if state.raw_mode {
            let _ = crossterm::terminal::disable_raw_mode();
        }
        state.raw_mode = false;
        state.alt_screen = false;
    });
    0
});

// Create or replace the shared console. Returns 0 on success.
export_int!(tui_init, args, {
    let color_system = args.string(0);
    with_state(|state| {
        let lowered = color_system.to_ascii_lowercase();
        state.color = Some(!(lowered.contains("none") || lowered.contains("nocolor")));
    });
    0
});

// Configure console width. Returns the applied width.
export_int!(tui_set_width, args, {
    let width = args.int(0);
    with_state(|state| {
        state.width = width.clamp(1, 1000) as u16;
        state.width as i64
    })
});

// Enable/disable markup. Returns 0 on success.
export_int!(tui_set_markup, args, {
    let enabled = args.int(0);
    with_state(|state| {
        state.markup = enabled != 0;
    });
    0
});

// Enable/disable emoji. Returns 0 on success.
export_int!(tui_set_emoji, args, {
    let enabled = args.int(0);
    with_state(|state| {
        state.emoji = enabled != 0;
    });
    0
});

// Enable/disable highlight. Returns 0 on success.
export_int!(tui_set_highlight, args, {
    let enabled = args.int(0);
    with_state(|state| {
        state.highlight = enabled != 0;
    });
    0
});

// Enable/disable soft wrap. Returns 0 on success.
export_int!(tui_set_soft_wrap, args, {
    let enabled = args.int(0);
    with_state(|state| {
        state.soft_wrap = enabled != 0;
    });
    0
});

// Log text to console. Returns 0 on success.
export_int!(tui_console_log, args, {
    let text = args.string(0);
    with_state(|state| {
        let rendered = if state.markup {
            let spans = markup_spans(text, Style::default());
            render_lines(state, vec![Line::from(spans)], text)
        } else {
            text.to_string()
        };
        emit(state, &rendered);
    });
    0
});

// Save console text to a file. Returns "ok" or "Error: <message>".
export_string!(tui_console_save_text, args, {
    let path = args.string(0);
    with_state(|state| match std::fs::write(path, console_text(state)) {
        Ok(()) => "ok".to_string(),
        Err(error) => format!("Error: {error}"),
    })
});

// Save console HTML to a file. Returns "ok" or "Error: <message>".
export_string!(tui_console_save_html, args, {
    let path = args.string(0);
    with_state(|state| {
        let body = state
            .lines
            .iter()
            .map(|line| escape_html(line))
            .collect::<Vec<_>>()
            .join("\n");
        let document = format!("<pre>{body}</pre>\n");
        match std::fs::write(path, document) {
            Ok(()) => "ok".to_string(),
            Err(error) => format!("Error: {error}"),
        }
    })
});

// Escape markup. Returns the escaped string.
export_string!(tui_markup_escape, args, {
    let text = args.string(0);
    text.replace('\\', "\\\\").replace('[', "\\[")
});

// Validate a style string. Returns true if valid.
export_int!(tui_style_valid, args, {
    let style = args.string(0);
    i64::from(parse_style(style).is_some())
});

// Print text with a style. Returns 0 on success.
export_int!(tui_print_text_style, args, {
    let text = args.string(0);
    let style = args.string(1);
    with_state(|state| {
        let parsed = parse_style(style).unwrap_or_default();
        let rendered = render_paragraph(state, text, parsed, Alignment::Left, None);
        emit(state, &rendered);
    });
    0
});

// Print plain text. Returns 0 on success.
export_int!(tui_print_text_plain, args, {
    let text = args.string(0);
    with_state(|state| {
        let rendered = render_paragraph(state, text, Style::default(), Alignment::Left, None);
        emit(state, &rendered);
    });
    0
});

// Pretty-print a value. Returns 0 on success.
export_int!(tui_print_pretty, args, {
    let value = args.string(0);
    with_state(|state| {
        let rendered = render_paragraph(
            state,
            &json_pretty(value),
            Style::default(),
            Alignment::Left,
            None,
        );
        emit(state, &rendered);
    });
    0
});

// Print columns. Returns 0 on success.
export_int!(tui_print_columns, args, {
    let items_json = args.string(0);
    let equal = args.int(0);
    let expand = args.int(1);
    with_state(|state| {
        let items = json_string_array(items_json);
        let rendered = render_columns(state, &items, equal != 0, expand != 0);
        emit(state, &rendered);
    });
    0
});

// Print aligned text. Returns 0 on success.
export_int!(tui_print_aligned, args, {
    let text = args.string(0);
    let align = args.string(1);
    let pad = args.int(0);
    with_state(|state| {
        let rendered = render_aligned(state.width as usize, text, align, pad != 0);
        emit(state, &rendered);
    });
    0
});

// Print padded text. Returns 0 on success.
export_int!(tui_print_padded, args, {
    let text = args.string(0);
    let top = args.int(0);
    let right = args.int(1);
    let bottom = args.int(2);
    let left = args.int(3);
    with_state(|state| {
        let rendered = pad(
            text,
            state.width as usize,
            top.max(0) as usize,
            right.max(0) as usize,
            bottom.max(0) as usize,
            left.max(0) as usize,
        );
        emit(state, &rendered);
    });
    0
});

// Print exception traceback. Returns 0 on success.
export_int!(tui_print_exception, args, {
    with_state(|state| {
        emit(state, "(no exception information available)");
    });
    0
});

// Install traceback. Returns 0 on success.
export_int!(tui_install_traceback, args, {
    let _show_locals = args.int(0);
    0
});

// ── Stateful tables ────────────────────────────────────────────────────────

// Create a table and return its handle (index), or -1 on error.
export_int!(tui_table_create, args, {
    let title = args.string(0);
    with_state(|state| {
        allocate(
            &mut state.tables,
            TableState {
                title: title.to_string(),
                columns: Vec::new(),
                rows: Vec::new(),
                caption: String::new(),
                show_header: true,
                show_lines: false,
                boxed: true,
                expand: false,
            },
        )
    })
});

// Add a column to a table. Returns 0 on success.
export_int!(tui_table_add_column, args, {
    let index = args.int(0);
    let header = args.string(0);
    let style = args.string(1);
    with_state(|state| {
        let Some(slot) = state
            .tables
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        slot.columns.push(Column {
            header: header.to_string(),
            style: parse_style(style).unwrap_or_default(),
        });
        0
    })
});

// Add a row to a table. Returns 0 on success.
export_int!(tui_table_add_row, args, {
    let index = args.int(0);
    let values_json = args.string(0);
    with_state(|state| {
        let Some(slot) = state
            .tables
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        slot.rows.push(json_row(values_json));
        0
    })
});

// Set table caption. Returns 0 on success.
export_int!(tui_table_set_caption, args, {
    let index = args.int(0);
    let caption = args.string(0);
    with_state(|state| {
        let Some(slot) = state
            .tables
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        slot.caption = caption.to_string();
        0
    })
});

// Set table header visibility. Returns 0 on success.
export_int!(tui_table_set_header, args, {
    let index = args.int(0);
    let show = args.int(1);
    with_state(|state| {
        let Some(slot) = state
            .tables
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        slot.show_header = show != 0;
        0
    })
});

// Set table lines visibility. Returns 0 on success.
export_int!(tui_table_set_lines, args, {
    let index = args.int(0);
    let show = args.int(1);
    with_state(|state| {
        let Some(slot) = state
            .tables
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        slot.show_lines = show != 0;
        0
    })
});

// Set table box style. Returns 0 on success.
export_int!(tui_table_set_box, args, {
    let index = args.int(0);
    let box_name = args.string(0);
    with_state(|state| {
        let Some(slot) = state
            .tables
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        slot.boxed = !box_name.is_empty() && box_name != "none";
        0
    })
});

// Set table expand. Returns 0 on success.
export_int!(tui_table_set_expand, args, {
    let index = args.int(0);
    let expand = args.int(1);
    with_state(|state| {
        let Some(slot) = state
            .tables
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        slot.expand = expand != 0;
        0
    })
});

// Print a table. Returns 0 on success.
export_int!(tui_table_print, args, {
    let index = args.int(0);
    with_state(|state| {
        let rendered = {
            let Some(spec) = state
                .tables
                .get(index as usize)
                .and_then(|slot| slot.as_ref())
            else {
                return -1;
            };
            render_table(state, spec)
        };
        emit(state, &rendered);
        0
    })
});

// ── Trees ──────────────────────────────────────────────────────────────────

// Create a tree and return its handle, or -1 on error.
export_int!(tui_tree_create, args, {
    let label = args.string(0);
    with_state(|state| {
        allocate(
            &mut state.trees,
            Node {
                label: label.to_string(),
                children: Vec::new(),
            },
        )
    })
});

// Add a child to a tree. Returns the child handle, or -1 on error.
export_int!(tui_tree_add, args, {
    let parent_index = args.int(0);
    let label = args.string(0);
    with_state(|state| {
        let parent = parent_index as usize;
        if state
            .trees
            .get(parent)
            .and_then(|slot| slot.as_ref())
            .is_none()
        {
            return -1;
        }
        let child = allocate(
            &mut state.trees,
            Node {
                label: label.to_string(),
                children: Vec::new(),
            },
        );
        if let Some(slot) = state.trees.get_mut(parent).and_then(|slot| slot.as_mut()) {
            slot.children.push(child as usize);
        }
        child
    })
});

// Print a tree. Returns 0 on success.
export_int!(tui_tree_print, args, {
    let index = args.int(0);
    with_state(|state| {
        let rendered = render_tree(&state.trees, index as usize);
        if rendered.is_empty() {
            return -1;
        }
        emit(state, &rendered);
        0
    })
});

// ── Layouts ────────────────────────────────────────────────────────────────

// Create a layout and return its handle, or -1 on error.
export_int!(tui_layout_create, args, {
    let _name = args.string(0);
    with_state(|state| {
        allocate(
            &mut state.layouts,
            LayoutState {
                rows: true,
                sections: Vec::new(),
            },
        )
    })
});

// Split layout rows. Returns 0 on success.
export_int!(tui_layout_split_rows, args, {
    let index = args.int(0);
    let names_json = args.string(0);
    with_state(|state| split_layout(state, index, names_json, true))
});

// Split layout columns. Returns 0 on success.
export_int!(tui_layout_split_columns, args, {
    let index = args.int(0);
    let names_json = args.string(0);
    with_state(|state| split_layout(state, index, names_json, false))
});

// Update layout section. Returns 0 on success.
export_int!(tui_layout_update, args, {
    let index = args.int(0);
    let name = args.string(0);
    let text = args.string(1);
    with_state(|state| {
        let Some(slot) = state
            .layouts
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        let section = ensure_section(slot, name);
        section.text = text.to_string();
        0
    })
});

// Set layout panel. Returns 0 on success.
export_int!(tui_layout_panel, args, {
    let index = args.int(0);
    let name = args.string(0);
    let text = args.string(1);
    let title = args.string(2);
    with_state(|state| {
        let Some(slot) = state
            .layouts
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        let section = ensure_section(slot, name);
        section.text = text.to_string();
        section.title = title.to_string();
        section.panel = true;
        0
    })
});

// Print a layout. Returns 0 on success.
export_int!(tui_layout_print, args, {
    let index = args.int(0);
    with_state(|state| {
        let rendered = {
            let Some(spec) = state
                .layouts
                .get(index as usize)
                .and_then(|slot| slot.as_ref())
            else {
                return -1;
            };
            render_layout(state, spec)
        };
        emit(state, &rendered);
        0
    })
});

// ── Progress ───────────────────────────────────────────────────────────────

// Start a progress bar. Returns the handle, or -1 on error.
export_int!(tui_progress_start, args, {
    with_state(|state| allocate(&mut state.progress, ProgressState { tasks: Vec::new() }))
});

// Add a task to a progress bar. Returns the task ID, or -1 on error.
export_int!(tui_progress_add_task, args, {
    let index = args.int(0);
    let description = args.string(0);
    let total = args.float(1) as f64;
    with_state(|state| {
        let Some(slot) = state
            .progress
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        slot.tasks.push(Task {
            description: description.to_string(),
            total,
            completed: 0.0,
        });
        (slot.tasks.len() - 1) as i64
    })
});

// Advance a progress task. Returns 0 on success.
export_int!(tui_progress_advance, args, {
    let index = args.int(0);
    let task_id = args.int(1);
    let amount = args.float(2) as f64;
    with_state(|state| {
        let width = state.width;
        let ansi = ansi_enabled(state);
        let rendered = {
            let Some(slot) = state
                .progress
                .get_mut(index as usize)
                .and_then(|slot| slot.as_mut())
            else {
                return -1;
            };
            let Some(task) = slot.tasks.get_mut(task_id as usize) else {
                return -1;
            };
            task.completed = (task.completed + amount).max(0.0);
            render_progress(width, ansi, slot)
        };
        emit(state, &rendered);
        0
    })
});

// Update a progress task. Returns 0 on success.
export_int!(tui_progress_update, args, {
    let index = args.int(0);
    let task_id = args.int(1);
    let completed = args.float(2) as f64;
    let total = args.float(3) as f64;
    with_state(|state| {
        let width = state.width;
        let ansi = ansi_enabled(state);
        let rendered = {
            let Some(slot) = state
                .progress
                .get_mut(index as usize)
                .and_then(|slot| slot.as_mut())
            else {
                return -1;
            };
            let Some(task) = slot.tasks.get_mut(task_id as usize) else {
                return -1;
            };
            task.completed = completed.max(0.0);
            if total > 0.0 {
                task.total = total;
            }
            render_progress(width, ansi, slot)
        };
        emit(state, &rendered);
        0
    })
});

// Stop a progress bar. Returns 0 on success.
export_int!(tui_progress_stop, args, {
    let index = args.int(0);
    with_state(|state| {
        let width = state.width;
        let ansi = ansi_enabled(state);
        let rendered = {
            let Some(slot) = state
                .progress
                .get(index as usize)
                .and_then(|slot| slot.as_ref())
            else {
                return -1;
            };
            render_progress(width, ansi, slot)
        };
        emit(state, &rendered);
        if let Some(slot) = state.progress.get_mut(index as usize) {
            *slot = None;
        }
        0
    })
});

// ── Status ─────────────────────────────────────────────────────────────────

// Start a status. Returns the handle, or -1 on error.
export_int!(tui_status_start, args, {
    let text = args.string(0);
    with_state(|state| allocate(&mut state.status, text.to_string()))
});

// Update a status. Returns 0 on success.
export_int!(tui_status_update, args, {
    let index = args.int(0);
    let text = args.string(0);
    with_state(|state| {
        let Some(slot) = state
            .status
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        *slot = text.to_string();
        let rendered = render_paragraph(state, text, Style::default(), Alignment::Left, None);
        emit(state, &rendered);
        0
    })
});

// Stop a status. Returns 0 on success.
export_int!(tui_status_stop, args, {
    let index = args.int(0);
    with_state(|state| {
        let text = state
            .status
            .get(index as usize)
            .and_then(|slot| slot.as_ref())
            .cloned();
        let Some(text) = text else {
            return -1;
        };
        let rendered = render_paragraph(state, &text, Style::default(), Alignment::Left, None);
        emit(state, &rendered);
        if let Some(slot) = state.status.get_mut(index as usize) {
            *slot = None;
        }
        0
    })
});

// ── Live ───────────────────────────────────────────────────────────────────

// Start a live display. Returns the handle, or -1 on error.
export_int!(tui_live_start, args, {
    let text = args.string(0);
    let _refresh = args.float(0);
    with_state(|state| {
        allocate(
            &mut state.live,
            LiveState {
                text: text.to_string(),
                title: String::new(),
                panel: false,
            },
        )
    })
});

// Update a live display. Returns 0 on success.
export_int!(tui_live_update, args, {
    let index = args.int(0);
    let text = args.string(0);
    with_state(|state| {
        let Some(slot) = state
            .live
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        slot.text = text.to_string();
        slot.panel = false;
        let rendered = render_paragraph(state, text, Style::default(), Alignment::Left, None);
        emit(state, &rendered);
        0
    })
});

// Update a live display with a panel. Returns 0 on success.
export_int!(tui_live_panel, args, {
    let index = args.int(0);
    let text = args.string(0);
    let title = args.string(1);
    with_state(|state| {
        let Some(slot) = state
            .live
            .get_mut(index as usize)
            .and_then(|slot| slot.as_mut())
        else {
            return -1;
        };
        slot.text = text.to_string();
        slot.title = title.to_string();
        slot.panel = true;
        let block = panel_block(title, Style::default());
        let rendered =
            render_paragraph(state, text, Style::default(), Alignment::Left, Some(block));
        emit(state, &rendered);
        0
    })
});

// Stop a live display. Returns 0 on success.
export_int!(tui_live_stop, args, {
    let index = args.int(0);
    with_state(|state| {
        let snapshot = state
            .live
            .get(index as usize)
            .and_then(|slot| slot.as_ref())
            .map(|slot| (slot.text.clone(), slot.title.clone(), slot.panel));
        let Some((text, title, panel)) = snapshot else {
            return -1;
        };
        let rendered = if panel {
            let block = panel_block(&title, Style::default());
            render_paragraph(state, &text, Style::default(), Alignment::Left, Some(block))
        } else {
            render_paragraph(state, &text, Style::default(), Alignment::Left, None)
        };
        emit(state, &rendered);
        if let Some(slot) = state.live.get_mut(index as usize) {
            *slot = None;
        }
        0
    })
});

// ── Prompt variants ────────────────────────────────────────────────────────

// Read a password. Returns the input string, or "Error: <message>".
export_string!(tui_ask_password, args, {
    let prompt = args.string(0);
    let _ = prompt;
    read_secret("")
});

// Read an integer. Returns the value, or 0 on error.
export_int!(tui_ask_int, args, {
    let prompt = args.string(0);
    let _ = prompt;
    parse_int(&read_line(""))
});

// Read a float. Returns the value, or 0.0 on error.
export_float!(tui_ask_float, args, {
    let prompt = args.string(0);
    let _ = prompt;
    parse_float(&read_line(""))
});

// Read input with a default. Returns the input string, or "Error: <message>".
export_string!(tui_ask_default, args, {
    let prompt = args.string(0);
    let default_value = args.string(1);
    let _ = prompt;
    read_line(default_value)
});

// Read a yes/no answer. Returns true/false.
export_int!(tui_confirm, args, {
    let prompt = args.string(0);
    let _ = prompt;
    i64::from(parse_confirm(&read_line(""), false))
});

// Read a yes/no answer, falling back to `defaultValue` when nothing is entered.
export_int!(tui_confirm_default, args, {
    let prompt = args.string(0);
    let default_value = args.int(0);
    let _ = prompt;
    i64::from(parse_confirm(&read_line(""), default_value != 0))
});

// --- Small helpers ---------------------------------------------------------

fn console_text(state: &TuiState) -> String {
    let mut text = state.lines.join("\n");
    if !text.is_empty() {
        text.push('\n');
    }
    text
}

fn json_string_array(text: &str) -> Vec<String> {
    match serde_json::from_str::<Value>(text) {
        Ok(Value::Array(items)) => items.iter().map(value_to_string).collect(),
        _ => Vec::new(),
    }
}

fn json_rows(text: &str) -> Vec<Vec<String>> {
    match serde_json::from_str::<Value>(text) {
        Ok(Value::Array(rows)) => rows
            .iter()
            .map(|row| match row {
                Value::Array(cells) => cells.iter().map(value_to_string).collect(),
                other => vec![value_to_string(other)],
            })
            .collect(),
        _ => Vec::new(),
    }
}

fn json_row(text: &str) -> Vec<String> {
    match serde_json::from_str::<Value>(text) {
        Ok(Value::Array(cells)) => cells.iter().map(value_to_string).collect(),
        Ok(other) => vec![value_to_string(&other)],
        Err(_) => vec![text.to_string()],
    }
}

fn value_to_string(value: &Value) -> String {
    match value {
        Value::String(text) => text.clone(),
        Value::Null => "null".to_string(),
        Value::Bool(flag) => flag.to_string(),
        Value::Number(number) => number.to_string(),
        other => other.to_string(),
    }
}

fn split_layout(state: &mut TuiState, index: i64, names_json: &str, rows: bool) -> i64 {
    let Some(slot) = state
        .layouts
        .get_mut(index as usize)
        .and_then(|slot| slot.as_mut())
    else {
        return -1;
    };
    slot.rows = rows;
    slot.sections = json_string_array(names_json)
        .into_iter()
        .map(|name| Section {
            name,
            text: String::new(),
            title: String::new(),
            panel: false,
        })
        .collect();
    0
}

fn ensure_section<'a>(layout: &'a mut LayoutState, name: &str) -> &'a mut Section {
    if let Some(position) = layout
        .sections
        .iter()
        .position(|section| section.name == name)
    {
        return &mut layout.sections[position];
    }
    layout.sections.push(Section {
        name: name.to_string(),
        text: String::new(),
        title: String::new(),
        panel: false,
    });
    let last = layout.sections.len() - 1;
    &mut layout.sections[last]
}

/// Reads a secret without echoing it when stdin is a terminal.
fn read_secret(default: &str) -> String {
    if std::io::stdin().is_tty() {
        let _ = crossterm::terminal::enable_raw_mode();
        let value = read_line(default);
        let _ = crossterm::terminal::disable_raw_mode();
        println!();
        value
    } else {
        read_line(default)
    }
}

// ── Extended widgets ───────────────────────────────────────────────────────

// Render a bulleted or numbered list. Returns 0 on success.
export_int!(tui_list, args, {
    let items_json = args.string(0);
    let title = args.string(1);
    let numbered = args.int(0);
    with_state(|state| {
        let items = json_string_array(items_json);
        let rendered = widgets_ext::list(
            state.width,
            ansi_enabled(state),
            &items,
            title,
            numbered != 0,
        );
        emit(state, &rendered);
    });
    0
});

// Render a row of tabs with one highlighted. Returns 0 on success.
export_int!(tui_tabs, args, {
    let labels_json = args.string(0);
    let active = args.int(0);
    with_state(|state| {
        let labels = json_string_array(labels_json);
        let rendered = widgets_ext::tabs(state.width, ansi_enabled(state), &labels, active);
        emit(state, &rendered);
    });
    0
});

// Render a bar chart from a JSON array of numbers. Returns 0 on success.
export_int!(tui_bar_chart, args, {
    let title = args.string(0);
    let data_json = args.string(1);
    let width = args.int(0);
    let height = args.int(1);
    with_state(|state| {
        let data = json_number_array(data_json);
        let rendered = widgets_ext::bar_chart(
            state.width,
            ansi_enabled(state),
            title,
            &data,
            width.max(0) as u16,
            height.max(0) as u16,
        );
        emit(state, &rendered);
    });
    0
});

// Render a sparkline from a JSON array of numbers. Returns 0 on success.
export_int!(tui_sparkline, args, {
    let data_json = args.string(0);
    with_state(|state| {
        let data = json_number_array(data_json);
        let rendered = widgets_ext::sparkline(state.width, ansi_enabled(state), &data);
        emit(state, &rendered);
    });
    0
});

// Render a month calendar. Returns 0 on success.
export_int!(tui_calendar, args, {
    let year = args.int(0);
    let month = args.int(1);
    with_state(|state| {
        let rendered = widgets_ext::calendar(year, month);
        emit(state, &rendered);
    });
    0
});

// Render JSON as an indented tree. Returns 0 on success.
export_int!(tui_json_tree, args, {
    let json_text = args.string(0);
    with_state(|state| {
        let rendered = match serde_json::from_str::<Value>(json_text) {
            Ok(value) => widgets_ext::json_tree(&value),
            Err(_) => json_text.to_string(),
        };
        emit(state, &rendered);
    });
    0
});

// Render a filled gauge. Returns 0 on success.
export_int!(tui_gauge, args, {
    let label = args.string(0);
    let ratio = args.float(0) as f64;
    let width = args.int(1);
    with_state(|state| {
        let render_width = if width <= 0 {
            state.width
        } else {
            (width as u16).min(state.width)
        };
        let rendered = widgets_ext::gauge(render_width, ansi_enabled(state), label, ratio);
        emit(state, &rendered);
    });
    0
});

// Render a one-line gauge. Returns 0 on success.
export_int!(tui_line_gauge, args, {
    let label = args.string(0);
    let ratio = args.float(0) as f64;
    let width = args.int(1);
    with_state(|state| {
        let render_width = if width <= 0 {
            state.width
        } else {
            (width as u16).min(state.width)
        };
        let rendered = widgets_ext::line_gauge(render_width, ansi_enabled(state), label, ratio);
        emit(state, &rendered);
    });
    0
});

// Render a table parsed from CSV. The first row becomes the header when it has
// more than one cell. Returns 0 on success.
export_int!(tui_table_from_csv, args, {
    let title = args.string(0);
    let csv_text = args.string(1);
    with_state(|state| {
        let rows = widgets_ext::csv_rows(csv_text);
        let columns = rows.iter().map(|row| row.len()).max().unwrap_or(0);
        let mut spec = TableState {
            title: title.to_string(),
            columns: (0..columns)
                .map(|_| Column {
                    header: String::new(),
                    style: Style::default(),
                })
                .collect(),
            rows,
            caption: String::new(),
            show_header: false,
            show_lines: false,
            boxed: true,
            expand: false,
        };
        if columns > 1 {
            if let Some(first) = spec.rows.first().cloned() {
                for (index, header) in first.iter().enumerate() {
                    if let Some(column) = spec.columns.get_mut(index) {
                        column.header = header.clone();
                    }
                }
                spec.rows.remove(0);
                spec.show_header = true;
            }
        }
        let rendered = render_table(state, &spec);
        emit(state, &rendered);
    });
    0
});

// ── Terminal control ───────────────────────────────────────────────────────

// The terminal width, or the configured render width without a TTY.
export_int!(tui_terminal_width, args, {
    with_state(|state| terminal_width(state) as i64)
});

// The terminal height, or a default without a TTY.
export_int!(tui_terminal_height, args, {
    with_state(|state| terminal_height(state) as i64)
});

// Move the cursor to an absolute position (TTY only). Returns 0.
export_int!(tui_set_cursor, args, {
    let x = args.int(0);
    let y = args.int(1);
    if is_tty() {
        let _ = crossterm::execute!(
            std::io::stdout(),
            crossterm::cursor::MoveTo(x.max(0) as u16, y.max(0) as u16)
        );
    }
    0
});

// Move the cursor relative to its position (TTY only). Returns 0.
export_int!(tui_move_cursor, args, {
    let dx = args.int(0);
    let dy = args.int(1);
    if is_tty() {
        let mut stdout = std::io::stdout();
        if dx > 0 {
            let _ = crossterm::execute!(stdout, crossterm::cursor::MoveRight(dx as u16));
        } else if dx < 0 {
            let _ = crossterm::execute!(stdout, crossterm::cursor::MoveLeft((-dx) as u16));
        }
        if dy > 0 {
            let _ = crossterm::execute!(stdout, crossterm::cursor::MoveDown(dy as u16));
        } else if dy < 0 {
            let _ = crossterm::execute!(stdout, crossterm::cursor::MoveUp((-dy) as u16));
        }
    }
    0
});

// Hide the cursor (TTY only). Returns 0.
export_int!(tui_hide_cursor, args, {
    if is_tty() {
        let _ = crossterm::execute!(std::io::stdout(), crossterm::cursor::Hide);
    }
    0
});

// Show the cursor (TTY only). Returns 0.
export_int!(tui_show_cursor, args, {
    if is_tty() {
        let _ = crossterm::execute!(std::io::stdout(), crossterm::cursor::Show);
    }
    0
});

// Ring the terminal bell (TTY only). Returns 0.
export_int!(tui_bell, args, {
    if is_tty() {
        let _ = crossterm::execute!(std::io::stdout(), crossterm::style::Print("\x07"));
    }
    0
});

// ── Input ──────────────────────────────────────────────────────────────────

// Read one key within `timeoutMs`, or "" without a TTY / on timeout.
export_string!(tui_input, args, {
    let timeout = args.int(0);
    read_key(timeout)
});

// Whether a key is waiting, within `timeoutMs`. False without a TTY.
export_int!(tui_poll_input, args, {
    let timeout = args.int(0);
    i64::from(poll_input(timeout))
});

// ── Selection prompts ──────────────────────────────────────────────────────

// Choose one of `choicesJson`; returns its index or -1.
export_int!(tui_select, args, {
    let prompt = args.string(0);
    let choices_json = args.string(1);
    let _ = prompt;
    let choices = json_string_array(choices_json);
    select_index(&choices)
});

// Choose several of `choicesJson`; returns a JSON array of indices.
export_string!(tui_multiselect, args, {
    let prompt = args.string(0);
    let choices_json = args.string(1);
    let _ = prompt;
    let choices = json_string_array(choices_json);
    multiselect_indices(&choices)
});

// Read multiple lines until a lone "." or EOF; returns the text.
export_string!(tui_editor, args, {
    let prompt = args.string(0);
    let default_text = args.string(1);
    let _ = prompt;
    read_multiline(default_text)
});

// ── Screen (full-screen live) and theme ────────────────────────────────────

// Start a full-screen display. Returns 0 on success.
export_int!(tui_screen_start, args, {
    let text = args.string(0);
    with_state(|state| {
        state.screen = Some(text.to_string());
        let rendered = render_paragraph(state, text, Style::default(), Alignment::Left, None);
        emit(state, &rendered);
    });
    0
});

// Update a full-screen display. Returns 0 on success.
export_int!(tui_screen_update, args, {
    let text = args.string(0);
    with_state(|state| {
        state.screen = Some(text.to_string());
        let rendered = render_paragraph(state, text, Style::default(), Alignment::Left, None);
        emit(state, &rendered);
    });
    0
});

// Stop a full-screen display. Returns 0 on success.
export_int!(tui_screen_stop, args, {
    with_state(|state| {
        if let Some(text) = state.screen.clone() {
            let rendered = render_paragraph(state, &text, Style::default(), Alignment::Left, None);
            emit(state, &rendered);
        }
        state.screen = None;
    });
    0
});

// Select a syntax highlighting theme. Returns true on success.
export_int!(tui_theme, args, {
    let name = args.string(0);
    with_state(|state| {
        let highlighter = state.highlighter();
        if name.is_empty() {
            1
        } else {
            i64::from(highlighter.set_theme(name))
        }
    })
});

// List the available highlighting themes as a JSON array. Returns the JSON.
export_string!(tui_theme_names, args, {
    with_state(|state| {
        let names = state.highlighter().theme_names();
        serde_json::to_string(&names).unwrap_or_else(|_| "[]".to_string())
    })
});

// The active highlighting theme name.
export_string!(tui_theme_name, args, {
    with_state(|state| state.highlighter().theme_name().to_string())
});

fn json_number_array(text: &str) -> Vec<u64> {
    match serde_json::from_str::<Value>(text) {
        Ok(Value::Array(items)) => items.iter().filter_map(Value::as_u64).collect(),
        _ => Vec::new(),
    }
}

fn terminal_width(state: &TuiState) -> u16 {
    if is_tty() {
        if let Ok((width, _)) = crossterm::terminal::size() {
            if width > 0 {
                return width;
            }
        }
    }
    state.width
}

fn terminal_height(state: &TuiState) -> u16 {
    if is_tty() {
        if let Ok((_, height)) = crossterm::terminal::size() {
            if height > 0 {
                return height;
            }
        }
    }
    let _ = state;
    24
}

fn poll_input(timeout_ms: i64) -> bool {
    if !is_tty() {
        return false;
    }
    let timeout = std::time::Duration::from_millis(timeout_ms.max(0) as u64);
    event::poll(timeout).unwrap_or(false)
}

fn read_key(timeout_ms: i64) -> String {
    if !is_tty() {
        return String::new();
    }
    let timeout = std::time::Duration::from_millis(timeout_ms.max(0) as u64);
    match event::poll(timeout) {
        Ok(true) => match event::read() {
            Ok(TermEvent::Key(key)) => key_name(key.code),
            _ => String::new(),
        },
        _ => String::new(),
    }
}

fn key_name(code: KeyCode) -> String {
    match code {
        KeyCode::Char(character) => character.to_string(),
        KeyCode::Enter => "enter".to_string(),
        KeyCode::Esc => "esc".to_string(),
        KeyCode::Tab => "tab".to_string(),
        KeyCode::Backspace => "backspace".to_string(),
        KeyCode::Up => "up".to_string(),
        KeyCode::Down => "down".to_string(),
        KeyCode::Left => "left".to_string(),
        KeyCode::Right => "right".to_string(),
        other => format!("{other:?}").to_ascii_lowercase(),
    }
}

fn select_index(choices: &[String]) -> i64 {
    if choices.is_empty() {
        return -1;
    }
    let answer = read_line("");
    let trimmed = answer.trim();
    if trimmed.is_empty() {
        return -1;
    }
    if let Ok(number) = trimmed.parse::<i64>() {
        if number >= 0 && (number as usize) < choices.len() {
            return number;
        }
    }
    choices
        .iter()
        .position(|choice| choice == trimmed)
        .map(|index| index as i64)
        .unwrap_or(-1)
}

fn multiselect_indices(choices: &[String]) -> String {
    let answer = read_line("");
    let mut chosen: Vec<i64> = Vec::new();
    for token in answer.split(',').map(str::trim).filter(|token| !token.is_empty()) {
        if let Ok(number) = token.parse::<i64>() {
            if number >= 0 && (number as usize) < choices.len() {
                chosen.push(number);
                continue;
            }
        }
        if let Some(index) = choices.iter().position(|choice| choice == token) {
            chosen.push(index as i64);
        }
    }
    let rendered: Vec<String> = chosen.iter().map(|index| index.to_string()).collect();
    format!("[{}]", rendered.join(","))
}

fn read_multiline(default: &str) -> String {
    let mut lines = Vec::new();
    loop {
        let mut buffer = String::new();
        match std::io::stdin().lock().read_line(&mut buffer) {
            Ok(0) => break,
            Ok(_) => {
                let trimmed = buffer.trim_end_matches(['\n', '\r']);
                if trimmed == "." {
                    break;
                }
                lines.push(trimmed.to_string());
            }
            Err(_) => break,
        }
    }
    if lines.is_empty() {
        default.to_string()
    } else {
        lines.join("\n")
    }
}

const OPS: &[(&str, &str, &str)] = &[
    // Existence and version
    ("exists", "tui_exists", "cdecl:int64(...)"),
    ("version", "tui_version", "cdecl:cstring(...)"),
    // Printing
    ("printText", "tui_print_text", "cdecl:int64(...)"),
    ("printStyled", "tui_print_styled", "cdecl:int64(...)"),
    ("markdown", "tui_markdown", "cdecl:int64(...)"),
    ("jsonPretty", "tui_json_pretty", "cdecl:int64(...)"),
    ("printSyntax", "tui_print_syntax", "cdecl:int64(...)"),
    ("printTextStyle", "tui_print_text_style", "cdecl:int64(...)"),
    ("printTextPlain", "tui_print_text_plain", "cdecl:int64(...)"),
    ("printPretty", "tui_print_pretty", "cdecl:int64(...)"),
    ("printColumns", "tui_print_columns", "cdecl:int64(...)"),
    ("printAligned", "tui_print_aligned", "cdecl:int64(...)"),
    ("printPadded", "tui_print_padded", "cdecl:int64(...)"),
    ("printException", "tui_print_exception", "cdecl:int64(...)"),
    (
        "installTraceback",
        "tui_install_traceback",
        "cdecl:int64(...)",
    ),
    // Panels and rules
    ("panel", "tui_panel", "cdecl:int64(...)"),
    ("panelStyled", "tui_panel_styled", "cdecl:int64(...)"),
    ("rule", "tui_rule", "cdecl:int64(...)"),
    ("ruleStyled", "tui_rule_styled", "cdecl:int64(...)"),
    // Table
    ("table", "tui_table", "cdecl:int64(...)"),
    ("tableCreate", "tui_table_create", "cdecl:int64(...)"),
    ("tableAddColumn", "tui_table_add_column", "cdecl:int64(...)"),
    ("tableAddRow", "tui_table_add_row", "cdecl:int64(...)"),
    (
        "tableSetCaption",
        "tui_table_set_caption",
        "cdecl:int64(...)",
    ),
    ("tableSetHeader", "tui_table_set_header", "cdecl:int64(...)"),
    ("tableSetLines", "tui_table_set_lines", "cdecl:int64(...)"),
    ("tableSetBox", "tui_table_set_box", "cdecl:int64(...)"),
    ("tableSetExpand", "tui_table_set_expand", "cdecl:int64(...)"),
    ("tablePrint", "tui_table_print", "cdecl:int64(...)"),
    // Tree
    ("treeCreate", "tui_tree_create", "cdecl:int64(...)"),
    ("treeAdd", "tui_tree_add", "cdecl:int64(...)"),
    ("treePrint", "tui_tree_print", "cdecl:int64(...)"),
    // Layout
    ("layoutCreate", "tui_layout_create", "cdecl:int64(...)"),
    (
        "layoutSplitRows",
        "tui_layout_split_rows",
        "cdecl:int64(...)",
    ),
    (
        "layoutSplitColumns",
        "tui_layout_split_columns",
        "cdecl:int64(...)",
    ),
    ("layoutUpdate", "tui_layout_update", "cdecl:int64(...)"),
    ("layoutPanel", "tui_layout_panel", "cdecl:int64(...)"),
    ("layoutPrint", "tui_layout_print", "cdecl:int64(...)"),
    // Progress
    ("progressStart", "tui_progress_start", "cdecl:int64(...)"),
    (
        "progressAddTask",
        "tui_progress_add_task",
        "cdecl:int64(...)",
    ),
    (
        "progressAdvance",
        "tui_progress_advance",
        "cdecl:int64(...)",
    ),
    ("progressUpdate", "tui_progress_update", "cdecl:int64(...)"),
    ("progressStop", "tui_progress_stop", "cdecl:int64(...)"),
    // Status
    ("statusStart", "tui_status_start", "cdecl:int64(...)"),
    ("statusUpdate", "tui_status_update", "cdecl:int64(...)"),
    ("statusStop", "tui_status_stop", "cdecl:int64(...)"),
    // Live
    ("liveStart", "tui_live_start", "cdecl:int64(...)"),
    ("liveUpdate", "tui_live_update", "cdecl:int64(...)"),
    ("livePanel", "tui_live_panel", "cdecl:int64(...)"),
    ("liveStop", "tui_live_stop", "cdecl:int64(...)"),
    // Console config
    ("init", "tui_init", "cdecl:int64(...)"),
    ("setWidth", "tui_set_width", "cdecl:int64(...)"),
    ("setMarkup", "tui_set_markup", "cdecl:int64(...)"),
    ("setEmoji", "tui_set_emoji", "cdecl:int64(...)"),
    ("setHighlight", "tui_set_highlight", "cdecl:int64(...)"),
    ("setSoftWrap", "tui_set_soft_wrap", "cdecl:int64(...)"),
    ("consoleLog", "tui_console_log", "cdecl:int64(...)"),
    (
        "consoleSaveText",
        "tui_console_save_text",
        "cdecl:cstring(...)",
    ),
    (
        "consoleSaveHtml",
        "tui_console_save_html",
        "cdecl:cstring(...)",
    ),
    // Markup and styles
    ("markupEscape", "tui_markup_escape", "cdecl:cstring(...)"),
    ("styleValid", "tui_style_valid", "cdecl:int64(...)"),
    // Clear and ask
    ("clear", "tui_clear", "cdecl:int64(...)"),
    ("ask", "tui_ask", "cdecl:cstring(...)"),
    ("askPassword", "tui_ask_password", "cdecl:cstring(...)"),
    ("askInt", "tui_ask_int", "cdecl:int64(...)"),
    ("askFloat", "tui_ask_float", "cdecl:float64(...)"),
    ("askDefault", "tui_ask_default", "cdecl:cstring(...)"),
    ("confirm", "tui_confirm", "cdecl:int64(...)"),
    ("confirmDefault", "tui_confirm_default", "cdecl:int64(...)"),
    // TUI mode
    ("enter", "tui_enter", "cdecl:int64(...)"),
    ("exit", "tui_exit", "cdecl:int64(...)"),
    // Extended widgets
    ("list", "tui_list", "cdecl:int64(...)"),
    ("tabs", "tui_tabs", "cdecl:int64(...)"),
    ("barChart", "tui_bar_chart", "cdecl:int64(...)"),
    ("sparkline", "tui_sparkline", "cdecl:int64(...)"),
    ("calendar", "tui_calendar", "cdecl:int64(...)"),
    ("jsonTree", "tui_json_tree", "cdecl:int64(...)"),
    ("gauge", "tui_gauge", "cdecl:int64(...)"),
    ("lineGauge", "tui_line_gauge", "cdecl:int64(...)"),
    ("tableFromCsv", "tui_table_from_csv", "cdecl:int64(...)"),
    // Terminal control
    ("terminalWidth", "tui_terminal_width", "cdecl:int64(...)"),
    ("terminalHeight", "tui_terminal_height", "cdecl:int64(...)"),
    ("setCursor", "tui_set_cursor", "cdecl:int64(...)"),
    ("moveCursor", "tui_move_cursor", "cdecl:int64(...)"),
    ("hideCursor", "tui_hide_cursor", "cdecl:int64(...)"),
    ("showCursor", "tui_show_cursor", "cdecl:int64(...)"),
    ("bell", "tui_bell", "cdecl:int64(...)"),
    // Input
    ("input", "tui_input", "cdecl:cstring(...)"),
    ("pollInput", "tui_poll_input", "cdecl:int64(...)"),
    // Selection prompts
    ("select", "tui_select", "cdecl:int64(...)"),
    ("multiselect", "tui_multiselect", "cdecl:cstring(...)"),
    ("editor", "tui_editor", "cdecl:cstring(...)"),
    // Screen and theme
    ("screenStart", "tui_screen_start", "cdecl:int64(...)"),
    ("screenUpdate", "tui_screen_update", "cdecl:int64(...)"),
    ("screenStop", "tui_screen_stop", "cdecl:int64(...)"),
    ("theme", "tui_theme", "cdecl:int64(...)"),
    ("themeNames", "tui_theme_names", "cdecl:cstring(...)"),
    ("themeName", "tui_theme_name", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);
