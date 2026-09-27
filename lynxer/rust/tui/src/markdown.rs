//! CommonMark rendering, backed by `pulldown-cmark`.
//!
//! Events are folded into `ratatui` lines: headings, paragraphs, emphasis,
//! inline code, fenced code (highlighted through `syntect`), block quotes,
//! ordered/unordered/task lists, rules, links and simple tables.

use crate::syntax::Highlighter;
use pulldown_cmark::{CodeBlockKind, Event, HeadingLevel, Options, Parser, Tag, TagEnd};
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};

pub fn render(text: &str, highlighter: &Highlighter, highlight: bool) -> Vec<Line<'static>> {
    let mut options = Options::empty();
    options.insert(Options::ENABLE_TABLES);
    options.insert(Options::ENABLE_STRIKETHROUGH);
    options.insert(Options::ENABLE_TASKLISTS);
    options.insert(Options::ENABLE_FOOTNOTES);

    let mut lines: Vec<Line<'static>> = Vec::new();
    let mut spans: Vec<Span<'static>> = Vec::new();
    let mut styles: Vec<Style> = vec![Style::default()];
    let mut line_started = false;

    let mut lists: Vec<Option<u64>> = Vec::new();
    let mut blockquote = 0usize;

    let mut in_code = false;
    let mut code_lang = String::new();
    let mut code = String::new();

    let mut row: Vec<String> = Vec::new();
    let mut rows: Vec<Vec<String>> = Vec::new();
    let mut header_rows = 0usize;
    let mut cell = String::new();
    let mut in_cell = false;

    for event in Parser::new_ext(text, options) {
        let current = *styles.last().unwrap();
        match event {
            Event::Start(tag) => match tag {
                Tag::Paragraph => {
                    start_line(&mut spans, &mut line_started, &quote_prefix(blockquote))
                }
                Tag::Heading { level, .. } => {
                    start_line(&mut spans, &mut line_started, &quote_prefix(blockquote));
                    let mut style = Style::default().add_modifier(Modifier::BOLD);
                    if matches!(level, HeadingLevel::H1) {
                        style = style.add_modifier(Modifier::UNDERLINED);
                    }
                    styles.push(style);
                }
                Tag::Emphasis => styles.push(current.add_modifier(Modifier::ITALIC)),
                Tag::Strong => styles.push(current.add_modifier(Modifier::BOLD)),
                Tag::Strikethrough => styles.push(current.add_modifier(Modifier::CROSSED_OUT)),
                Tag::Link { .. } => styles.push(
                    Style::default()
                        .fg(Color::Blue)
                        .add_modifier(Modifier::UNDERLINED),
                ),
                Tag::BlockQuote(_) => blockquote += 1,
                Tag::List(start) => lists.push(start),
                Tag::Item => {
                    let marker = match lists.last_mut() {
                        Some(Some(number)) => {
                            let marker = format!("{number}. ");
                            *number += 1;
                            marker
                        }
                        _ => "• ".to_string(),
                    };
                    let indent = "  ".repeat(lists.len().saturating_sub(1));
                    let prefix = format!("{}{}{}", quote_prefix(blockquote), indent, marker);
                    start_line(&mut spans, &mut line_started, &prefix);
                }
                Tag::CodeBlock(kind) => {
                    flush(&mut lines, &mut spans, &mut line_started);
                    in_code = true;
                    code.clear();
                    code_lang = match kind {
                        CodeBlockKind::Fenced(lang) => lang.to_string(),
                        CodeBlockKind::Indented => String::new(),
                    };
                }
                Tag::Table(_) => {
                    flush(&mut lines, &mut spans, &mut line_started);
                    rows.clear();
                    header_rows = 0;
                }
                Tag::TableHead => header_rows = rows.len().max(1),
                Tag::TableRow => row.clear(),
                Tag::TableCell => {
                    in_cell = true;
                    cell.clear();
                }
                _ => {}
            },
            Event::End(tag) => match tag {
                TagEnd::Paragraph => flush(&mut lines, &mut spans, &mut line_started),
                TagEnd::Heading(_) => {
                    flush(&mut lines, &mut spans, &mut line_started);
                    lines.push(Line::from(""));
                    styles.pop();
                }
                TagEnd::Emphasis | TagEnd::Strong | TagEnd::Strikethrough | TagEnd::Link => {
                    styles.pop();
                }
                TagEnd::BlockQuote(_) => blockquote = blockquote.saturating_sub(1),
                TagEnd::List(_) => {
                    lists.pop();
                }
                TagEnd::Item => flush(&mut lines, &mut spans, &mut line_started),
                TagEnd::CodeBlock => {
                    emit_code(&mut lines, &code, &code_lang, highlighter, highlight);
                    in_code = false;
                }
                TagEnd::TableRow => rows.push(std::mem::take(&mut row)),
                TagEnd::TableCell => {
                    row.push(std::mem::take(&mut cell));
                    in_cell = false;
                }
                TagEnd::Table => {
                    emit_table(&mut lines, &rows, header_rows);
                }
                _ => {}
            },
            Event::Text(text) => {
                if in_code {
                    code.push_str(&text);
                } else if in_cell {
                    cell.push_str(&text);
                } else {
                    push_text(
                        &mut spans,
                        &mut line_started,
                        &quote_prefix(blockquote),
                        current,
                        &text,
                    );
                }
            }
            Event::Code(text) => {
                if in_cell {
                    cell.push_str(&text);
                } else {
                    push_span(
                        &mut spans,
                        &mut line_started,
                        &quote_prefix(blockquote),
                        Span::styled(text.to_string(), Style::default().fg(Color::Yellow)),
                    );
                }
            }
            Event::SoftBreak => {
                if in_code {
                    code.push('\n');
                } else if in_cell {
                    cell.push(' ');
                } else {
                    push_text(
                        &mut spans,
                        &mut line_started,
                        &quote_prefix(blockquote),
                        current,
                        " ",
                    );
                }
            }
            Event::HardBreak => {
                if in_code {
                    code.push('\n');
                } else {
                    flush(&mut lines, &mut spans, &mut line_started);
                }
            }
            Event::Rule => {
                flush(&mut lines, &mut spans, &mut line_started);
                lines.push(Line::from("─".repeat(40)));
            }
            Event::TaskListMarker(done) => {
                let marker = if done { "[x] " } else { "[ ] " };
                push_text(&mut spans, &mut line_started, "", Style::default(), marker);
            }
            Event::FootnoteReference(name) => {
                push_text(
                    &mut spans,
                    &mut line_started,
                    &quote_prefix(blockquote),
                    Style::default().fg(Color::DarkGray),
                    &format!("[^{name}]"),
                );
            }
            Event::InlineHtml(_) | Event::Html(_) => {}
            _ => {}
        }
    }

    flush(&mut lines, &mut spans, &mut line_started);
    while lines.last().is_some_and(|line| line.spans.is_empty()) {
        lines.pop();
    }
    if lines.is_empty() {
        lines.push(Line::from(""));
    }
    lines
}

fn quote_prefix(depth: usize) -> String {
    "│ ".repeat(depth)
}

fn start_line(spans: &mut Vec<Span<'static>>, line_started: &mut bool, prefix: &str) {
    if !*line_started {
        if !prefix.is_empty() {
            spans.push(Span::raw(prefix.to_string()));
        }
        *line_started = true;
    }
}

fn push_text(
    spans: &mut Vec<Span<'static>>,
    line_started: &mut bool,
    prefix: &str,
    style: Style,
    text: &str,
) {
    start_line(spans, line_started, prefix);
    spans.push(Span::styled(text.to_string(), style));
}

fn push_span(
    spans: &mut Vec<Span<'static>>,
    line_started: &mut bool,
    prefix: &str,
    span: Span<'static>,
) {
    start_line(spans, line_started, prefix);
    spans.push(span);
}

fn flush(lines: &mut Vec<Line<'static>>, spans: &mut Vec<Span<'static>>, line_started: &mut bool) {
    if *line_started || !spans.is_empty() {
        lines.push(Line::from(std::mem::take(spans)));
    }
    *line_started = false;
}

fn emit_code(
    lines: &mut Vec<Line<'static>>,
    code: &str,
    lang: &str,
    highlighter: &Highlighter,
    highlight: bool,
) {
    let trimmed = code.strip_suffix('\n').unwrap_or(code);
    if highlight && !lang.trim().is_empty() {
        lines.extend(highlighter.block_lines(trimmed, lang.trim(), false));
    } else {
        for line in trimmed.split('\n') {
            lines.push(Line::from(format!("    {line}")));
        }
    }
}

fn emit_table(lines: &mut Vec<Line<'static>>, rows: &[Vec<String>], header_rows: usize) {
    if rows.is_empty() {
        return;
    }
    let columns = rows.iter().map(|row| row.len()).max().unwrap_or(0);
    let mut widths = vec![0usize; columns];
    for row in rows {
        for (index, value) in row.iter().enumerate() {
            widths[index] = widths[index].max(crate::style::text_width(value));
        }
    }
    for (row_index, row) in rows.iter().enumerate() {
        let mut line = String::new();
        for column in 0..columns {
            let value = row.get(column).map(String::as_str).unwrap_or("");
            line.push_str(value);
            if column + 1 < columns {
                let padding = widths[column].saturating_sub(crate::style::text_width(value));
                line.push_str(&" ".repeat(padding));
                line.push_str(" │ ");
            }
        }
        lines.push(Line::from(line.trim_end().to_string()));
        if row_index + 1 == header_rows {
            let separator = widths
                .iter()
                .map(|width| "─".repeat(*width))
                .collect::<Vec<_>>()
                .join("─┼─");
            lines.push(Line::from(separator));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::syntax::Highlighter;

    #[test]
    fn renders_headings_and_lists() {
        let highlighter = Highlighter::new();
        let lines = render("# Title\n\n- one\n- two", &highlighter, true);
        let text: Vec<String> = lines
            .iter()
            .map(|line| line.spans.iter().map(|s| s.content.as_ref()).collect())
            .collect();
        assert_eq!(text[0], "Title");
        assert!(text.iter().any(|line| line == "• one"));
        assert!(text.iter().any(|line| line == "• two"));
    }
}
