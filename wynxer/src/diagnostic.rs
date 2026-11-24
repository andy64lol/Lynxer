//! Shared diagnostic type used by every stage of the compiler.

#[derive(Debug, Clone)]
pub enum Severity {
    Error,
    Warning,
    Note,
}

impl Severity {
    pub fn label(&self) -> &'static str {
        match self {
            Severity::Error => "error",
            Severity::Warning => "warning",
            Severity::Note => "note",
        }
    }
}

#[derive(Debug, Clone)]
pub struct Diagnostic {
    pub severity: Severity,
    pub message: String,
    pub file: Option<String>,
    pub line: usize,
    pub column: usize,
}

impl Diagnostic {
    pub fn error(message: impl Into<String>) -> Self {
        Self {
            severity: Severity::Error,
            message: message.into(),
            file: None,
            line: 0,
            column: 0,
        }
    }

    pub fn warning(message: impl Into<String>) -> Self {
        Self {
            severity: Severity::Warning,
            message: message.into(),
            file: None,
            line: 0,
            column: 0,
        }
    }

    pub fn note(message: impl Into<String>) -> Self {
        Self {
            severity: Severity::Note,
            message: message.into(),
            file: None,
            line: 0,
            column: 0,
        }
    }

    /// Attach a source location (file, line, column) to a diagnostic.
    pub fn at(mut self, file: Option<&str>, line: usize, column: usize) -> Self {
        self.file = file.map(str::to_string);
        self.line = line;
        self.column = column;
        self
    }

    /// Render a diagnostic with a source excerpt and caret:
    ///
    /// ```text
    /// file.wynx:3:5: error: expected ';'
    ///   |
    /// 3 | int x = 5
    ///   |     ^ expected ';'
    /// ```
    pub fn render(&self, source: Option<&str>) -> String {
        let file = self.file.as_deref().unwrap_or("<input>");
        let mut out = if self.line > 0 && self.column > 0 {
            format!("{}:{}:{}: {}: {}", file, self.line, self.column, self.severity.label(), self.message)
        } else {
            format!("{}: {}: {}", file, self.severity.label(), self.message)
        };
        if let Some(source) = source {
            if self.line > 0 {
                if let Some(text) = source.lines().nth(self.line - 1) {
                    let gutter = format!("{:>width$}", self.line, width = 4);
                    let pad = " ".repeat(self.column.saturating_sub(1));
                    out.push('\n');
                    out.push_str("     |\n");
                    out.push_str(&format!("{} | {}\n", gutter, text));
                    out.push_str(&format!("     | {}^ {}", pad, self.message));
                }
            }
        }
        out
    }
}

/// Parse a trailing `at L:C` location from a lexer/parser error string and
/// render it with the source line + caret. If no location is present, the
/// message is returned with an `<input>` prefix.
pub fn format_parse_error(err: &str, source: &str, file: Option<&str>) -> String {
    let (message, line, column) = extract_location(err);
    Diagnostic::error(message)
        .at(file, line, column)
        .render(Some(source))
}

/// Extract a `... at <line>:<column>` suffix from an error string.
pub fn extract_location(err: &str) -> (String, usize, usize) {
    if let Some(pos) = err.rfind(" at ") {
        let msg = &err[..pos];
        let loc = &err[pos + 4..];
        if let Some((l, c)) = loc.split_once(':') {
            if let (Ok(line), Ok(column)) = (l.parse::<usize>(), c.parse::<usize>()) {
                return (msg.to_string(), line, column);
            }
        }
    }
    (err.to_string(), 0, 0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_extract_location() {
        let (msg, line, col) = extract_location("expected ';' at 3:5");
        assert_eq!(msg, "expected ';'");
        assert_eq!(line, 3);
        assert_eq!(col, 5);
    }

    #[test]
    fn test_render_with_caret() {
        let d = Diagnostic::error("expected ';'").at(Some("x.wynx"), 2, 6);
        let rendered = d.render(Some("int x = 5\nint y = 6\n"));
        assert!(rendered.contains("x.wynx:2:6: error: expected ';'"));
        assert!(rendered.contains("int y = 6"));
        assert!(rendered.contains("^ expected ';'"));
    }
}
