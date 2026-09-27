//! Sublime-syntax highlighting, backed by `syntect`.
//!
//! The syntax and theme sets are embedded, so no files are read at run time and
//! no system oniguruma is needed (the crate uses the pure-Rust `fancy-regex`
//! backend). Highlighting is applied to every line of a code block; the
//! resulting spans carry RGB colors, which the renderer strips when stdout is
//! not a color-capable terminal.

use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use syntect::easy::HighlightLines;
use syntect::highlighting::{FontStyle, ThemeSet};
use syntect::parsing::SyntaxSet;

/// The theme used until `theme()` names another one.
pub const DEFAULT_THEME: &str = "base16-ocean.dark";

pub struct Highlighter {
    syntaxes: SyntaxSet,
    themes: ThemeSet,
    theme_name: String,
}

impl Highlighter {
    pub fn new() -> Self {
        Highlighter {
            syntaxes: SyntaxSet::load_defaults_newlines(),
            themes: ThemeSet::load_defaults(),
            theme_name: DEFAULT_THEME.to_string(),
        }
    }

    /// The available theme names, sorted.
    pub fn theme_names(&self) -> Vec<String> {
        let mut names: Vec<String> = self.themes.themes.keys().cloned().collect();
        names.sort();
        names
    }

    /// Switches the active theme. Returns false for an unknown name.
    pub fn set_theme(&mut self, name: &str) -> bool {
        if self.themes.themes.contains_key(name) {
            self.theme_name = name.to_string();
            true
        } else {
            false
        }
    }

    pub fn theme_name(&self) -> &str {
        &self.theme_name
    }

    fn syntax_for(&self, lexer: &str) -> &syntect::parsing::SyntaxReference {
        self.syntaxes
            .find_syntax_by_token(lexer)
            .or_else(|| self.syntaxes.find_syntax_by_extension(lexer))
            .or_else(|| self.syntaxes.find_syntax_by_name(lexer))
            .unwrap_or_else(|| self.syntaxes.find_syntax_plain_text())
    }

    fn theme(&self) -> &syntect::highlighting::Theme {
        self.themes
            .themes
            .get(&self.theme_name)
            .or_else(|| self.themes.themes.values().next())
            .expect("syntect ships at least one theme")
    }

    /// Highlights a whole code block, optionally prefixed with line numbers.
    pub fn block_lines(&self, code: &str, lexer: &str, line_numbers: bool) -> Vec<Line<'static>> {
        let syntax = self.syntax_for(lexer);
        let mut highlighter = HighlightLines::new(syntax, self.theme());
        let mut lines = Vec::new();
        for (index, raw) in code.split('\n').enumerate() {
            let mut spans = Vec::new();
            if line_numbers {
                spans.push(Span::styled(
                    format!("{:>3} │ ", index + 1),
                    Style::default().fg(Color::DarkGray),
                ));
            }
            match highlighter.highlight_line(raw, &self.syntaxes) {
                Ok(regions) => {
                    for (style, text) in regions {
                        spans.push(Span::styled(text.to_string(), convert(style)));
                    }
                }
                Err(_) => spans.push(Span::raw(raw.to_string())),
            }
            if spans.is_empty() {
                spans.push(Span::raw(""));
            }
            lines.push(Line::from(spans));
        }
        if lines.is_empty() {
            lines.push(Line::from(""));
        }
        lines
    }
}

impl Default for Highlighter {
    fn default() -> Self {
        Self::new()
    }
}

fn convert(style: syntect::highlighting::Style) -> Style {
    let mut out = Style::default().fg(Color::Rgb(
        style.foreground.r,
        style.foreground.g,
        style.foreground.b,
    ));
    if style.font_style.contains(FontStyle::BOLD) {
        out = out.add_modifier(Modifier::BOLD);
    }
    if style.font_style.contains(FontStyle::ITALIC) {
        out = out.add_modifier(Modifier::ITALIC);
    }
    if style.font_style.contains(FontStyle::UNDERLINE) {
        out = out.add_modifier(Modifier::UNDERLINED);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn highlights_a_known_language() {
        let highlighter = Highlighter::new();
        let lines = highlighter.block_lines("let x = 1;", "rust", true);
        assert_eq!(lines.len(), 1);
        let text: String = lines[0]
            .spans
            .iter()
            .map(|span| span.content.as_ref())
            .collect();
        assert!(text.contains("let x = 1;"));
    }

    #[test]
    fn theme_names_and_switch() {
        let mut highlighter = Highlighter::new();
        assert!(highlighter
            .theme_names()
            .iter()
            .any(|name| name == DEFAULT_THEME));
        assert!(highlighter.set_theme(DEFAULT_THEME));
        assert!(!highlighter.set_theme("definitely-not-a-theme"));
    }
}
