//! Parse `lynxer --lint` output into structured diagnostics.
//!
//! Real output looks like:
//!   `file.lynx:2:5: warning[lintUnusedVariable]: unused variable 'x'`
//!   `lynxer: file.lynx:3:1: [parserExpected] expected an expression`
//! plus context/help lines and a `Lint OK: ... (N warning(s))` summary.

use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Severity {
    Error,
    Warning,
    Info,
}

#[derive(Debug, Clone)]
pub struct Diagnostic {
    pub path: PathBuf,
    pub severity: Severity,
    /// 1-based; 0 means the diagnostic has no source location.
    pub line: usize,
    pub column: usize,
    pub code: String,
    pub message: String,
}

/// Find `:LINE:COL:` and return (index just after it, line, column).
fn find_position(line: &str) -> Option<(usize, usize, usize)> {
    let b = line.as_bytes();
    let mut i = 0usize;
    while i < b.len() {
        if b[i] == b':' {
            let mut j = i + 1;
            let num_start = j;
            while j < b.len() && b[j].is_ascii_digit() {
                j += 1;
            }
            if j > num_start && j < b.len() && b[j] == b':' {
                let l: usize = line[num_start..j].parse().ok()?;
                let mut k = j + 1;
                let col_start = k;
                while k < b.len() && b[k].is_ascii_digit() {
                    k += 1;
                }
                if k > col_start && k < b.len() && b[k] == b':' {
                    let c: usize = line[col_start..k].parse().ok()?;
                    return Some((k + 1, l, c));
                }
            }
        }
        i += 1;
    }
    None
}

/// Split `[CODE]: message` (lint) or `[CODE] message` (source errors).
fn split_code(rest: &str) -> Option<(String, String)> {
    let open = rest.find('[')?;
    let close = open + rest[open..].find(']')?;
    let code = rest[open + 1..close].to_string();
    let after = rest[close + 1..].trim_start();
    let message = after.strip_prefix(": ").unwrap_or(after).trim().to_string();
    Some((code, message))
}

pub fn parse(output: &str, path: &Path) -> Vec<Diagnostic> {
    let mut out = Vec::new();
    for raw in output.lines() {
        let line = raw.trim_end();
        if line.is_empty() {
            continue;
        }
        let trimmed = line.trim_start();
        if trimmed.starts_with("Lint OK")
            || trimmed.starts_with("help:")
            || trimmed.starts_with('|')
            || trimmed.starts_with("lynxer: note:")
        {
            continue;
        }

        if let Some((after, l, c)) = find_position(line) {
            let rest = line[after..].trim_start();
            let (severity, code, message) = if rest.starts_with("warning[") {
                let (code, message) = split_code(rest).unwrap_or_default();
                (Severity::Warning, code, message)
            } else if rest.starts_with("error[") {
                let (code, message) = split_code(rest).unwrap_or_default();
                (Severity::Error, code, message)
            } else if rest.starts_with('[') {
                // `lynxer: file:line:col: [code] message` (parser/source errors)
                let (code, message) = split_code(rest).unwrap_or_default();
                (Severity::Error, code, message)
            } else if trimmed.starts_with("lynxer:") {
                (Severity::Error, String::new(), rest.to_string())
            } else {
                (Severity::Info, String::new(), rest.to_string())
            };
            if message.is_empty() {
                continue;
            }
            out.push(Diagnostic {
                path: path.to_path_buf(),
                severity,
                line: l,
                column: c,
                code,
                message,
            });
        } else if trimmed.starts_with("lynxer:") {
            out.push(Diagnostic {
                path: path.to_path_buf(),
                severity: Severity::Error,
                line: 0,
                column: 0,
                code: String::new(),
                message: trimmed.trim_start_matches("lynxer:").trim().to_string(),
            });
        }
    }
    out
}

pub fn counts(items: &[Diagnostic]) -> (usize, usize) {
    let errors = items
        .iter()
        .filter(|d| d.severity == Severity::Error)
        .count();
    let warnings = items
        .iter()
        .filter(|d| d.severity == Severity::Warning)
        .count();
    (errors, warnings)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_lint_warning() {
        let text = "lynxer/examples/hello.lynx:2:5: warning[lintUnusedVariable]: unused variable 'greeting'\nLint OK: file (1 warning(s))\n";
        let d = parse(text, Path::new("x.lynx"));
        assert_eq!(d.len(), 1);
        assert_eq!(d[0].severity, Severity::Warning);
        assert_eq!((d[0].line, d[0].column), (2, 5));
        assert_eq!(d[0].code, "lintUnusedVariable");
        assert!(d[0].message.contains("unused variable"));
    }

    #[test]
    fn parses_source_error_and_skips_context() {
        let text = "lynxer: /tmp/bad.lynx:3:1: [parserExpected] expected an expression\n  |\n3 | }\n  | ^\nhelp: Check the nearby syntax.\n";
        let d = parse(text, Path::new("bad.lynx"));
        assert_eq!(d.len(), 1);
        assert_eq!(d[0].severity, Severity::Error);
        assert_eq!((d[0].line, d[0].column), (3, 1));
        assert_eq!(d[0].code, "parserExpected");
    }

    #[test]
    fn parses_lint_error_severity() {
        let d = parse("f.lynx:1:2: error[lintX]: boom\n", Path::new("f.lynx"));
        assert_eq!(d[0].severity, Severity::Error);
        assert_eq!(d[0].code, "lintX");
    }

    #[test]
    fn parses_missing_file_error() {
        let d = parse(
            "lynxer: file not found: 'nope.lynx'\n",
            Path::new("nope.lynx"),
        );
        assert_eq!(d.len(), 1);
        assert_eq!(d[0].severity, Severity::Error);
        assert_eq!(d[0].line, 0);
    }

    #[test]
    fn counts_severities() {
        let d = parse("f:1:1: warning[w]: w\nf:2:1: error[e]: e\n", Path::new("f"));
        assert_eq!(counts(&d), (1, 1));
    }
}
