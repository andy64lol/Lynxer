//! Source formatter for Wynxer scripts.
//!
//! Token-aware, line-based pretty printer: normalises indentation (two spaces
//! per brace level), spacing around operators, and one statement per line.
//! Comments and string literals are preserved verbatim.

/// Format a Wynxer source string. Returns the formatted source.
pub fn format_script(source: &str) -> String {
    let mut out = String::new();
    let mut depth = 0usize;

    // Split statements that share a line (e.g. `fn f() { int x = 5; }`).
    let mut logical_lines: Vec<String> = Vec::new();
    for raw in source.lines() {
        for piece in split_statements(raw) {
            logical_lines.push(piece);
        }
    }

    for raw in logical_lines {
        let line = raw.trim();
        if line.is_empty() {
            continue;
        }
        // Preserve comments verbatim (indent them to the current level).
        if line.starts_with("//") || line.starts_with("/*") || line.starts_with('*') {
            out.push_str(&indent(depth));
            out.push_str(line);
            out.push('\n');
            continue;
        }

        let closes = line.chars().take_while(|c| *c == '}').count();
        if closes > 0 {
            depth = depth.saturating_sub(closes);
        }

        let formatted = format_line(line);
        if !formatted.is_empty() {
            out.push_str(&indent(depth));
            out.push_str(&formatted);
            out.push('\n');
        }

        let opens = count_opens(line).saturating_sub(count_closes(line));
        if opens > 0 {
            depth += opens;
        }
    }
    out
}

fn indent(depth: usize) -> String {
    "  ".repeat(depth)
}

/// Split a raw line into logical statements at `;` and standalone braces,
/// while respecting quoted strings and comments.
fn split_statements(line: &str) -> Vec<String> {
    let mut parts = Vec::new();
    let mut current = String::new();
    let mut in_str = false;
    let mut chars = line.chars().peekable();
    while let Some(c) = chars.next() {
        if in_str {
            current.push(c);
            if c == '"' {
                in_str = false;
            }
            continue;
        }
        match c {
            '"' => {
                in_str = true;
                current.push(c);
            }
            '/' if chars.peek() == Some(&'/') => {
                // Rest of the line is a comment.
                current.push(c);
                for ch in chars.by_ref() {
                    current.push(ch);
                }
                break;
            }
            ';' | '{' | '}' => {
                current.push(c);
                let p = current.trim().to_string();
                if !p.is_empty() {
                    parts.push(p);
                }
                current.clear();
            }
            _ => current.push(c),
        }
    }
    let p = current.trim().to_string();
    if !p.is_empty() {
        parts.push(p);
    }
    parts
}

fn count_opens(line: &str) -> usize {
    count_outside_strings(line, '{')
}

fn count_closes(line: &str) -> usize {
    count_outside_strings(line, '}')
}

fn count_outside_strings(line: &str, target: char) -> usize {
    let mut count = 0;
    let mut in_str = false;
    for c in line.chars() {
        if in_str {
            if c == '"' {
                in_str = false;
            }
            continue;
        }
        if c == '"' {
            in_str = true;
        } else if c == target {
            count += 1;
        }
    }
    count
}

/// Normalise spacing in a single logical line.
fn format_line(line: &str) -> String {
    let mut out = String::new();
    let mut in_str = false;
    let chars: Vec<char> = line.chars().collect();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if in_str {
            out.push(c);
            if c == '"' {
                in_str = false;
            }
            i += 1;
            continue;
        }
        if c == '"' {
            in_str = true;
            out.push(c);
            i += 1;
            continue;
        }
        // Collapse runs of whitespace to a single space.
        if c.is_whitespace() {
            if !out.ends_with(' ') {
                out.push(' ');
            }
            i += 1;
            continue;
        }
        // Normalise `=`, `+`, etc. with spaces, except unary/`==`/`!=`.
        if "=+-*/%<>".contains(c) {
            let prev = chars.get(i.wrapping_sub(1)).copied().unwrap_or(' ');
            let next = chars.get(i + 1).copied().unwrap_or(' ');
            // Keep two-character operators together.
            if (c == '=' && next == '=')
                || (c == '!' && next == '=')
                || (c == '<' && next == '=')
                || (c == '>' && next == '=')
                || (c == '&' && next == '&')
                || (c == '|' && next == '|')
            {
                out.push(c);
                out.push(next);
                i += 2;
                continue;
            }
            // Unary minus/plus at start or after an operator/open paren.
            let unary = (c == '-' || c == '+')
                && (out.trim_end().is_empty()
                    || out.trim_end().ends_with(['(', '=', '+', '-', '*', '/', '<', '>']));
            if unary {
                out.push(c);
            } else {
                if !out.ends_with(' ') {
                    out.push(' ');
                }
                out.push(c);
                out.push(' ');
            }
            let _ = prev;
            i += 1;
            continue;
        }
        out.push(c);
        i += 1;
    }
    out.trim().to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_indentation() {
        let src = "fn f() {\nint x = 5;\n}\n";
        let formatted = format_script(src);
        assert!(formatted.contains("fn f() {"));
        assert!(formatted.contains("  var x = 5;") == false); // no var rewriting
        assert!(formatted.contains("  int x = 5;"));
        assert!(formatted.contains("}"));
    }

    #[test]
    fn test_one_line_split() {
        let src = "fn f() { int x = 5; }";
        let formatted = format_script(src);
        assert!(formatted.lines().count() >= 3);
        assert!(formatted.contains("  int x = 5;"));
    }

    #[test]
    fn test_preserves_strings() {
        let src = "fn f() { println(\"a  b\"); }";
        let formatted = format_script(src);
        assert!(formatted.contains("\"a  b\""));
    }
}
