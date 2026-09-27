//! Prompt input handling.
//!
//! Prompts always read from stdin. When stdin is not a terminal (a pipe, a
//! redirected file, CI) a read returns immediately at end-of-file, so a script
//! can never block waiting for input that will not come; the caller supplies
//! the default used in that case.

use std::io::{self, BufRead};

/// Reads one line from stdin. Returns `default` at end-of-file or on an empty
/// line, so `ask("")` and `askDefault(d)` share one code path.
pub fn read_line(default: &str) -> String {
    let mut buffer = String::new();
    match io::stdin().lock().read_line(&mut buffer) {
        Ok(0) => default.to_string(),
        Ok(_) => {
            let trimmed = buffer.trim_end_matches(['\n', '\r']).to_string();
            if trimmed.is_empty() {
                default.to_string()
            } else {
                trimmed
            }
        }
        Err(_) => default.to_string(),
    }
}

/// Parses an integer, returning `0` for anything unparsable.
pub fn parse_int(value: &str) -> i64 {
    value.trim().parse().unwrap_or(0)
}

/// Parses a float, returning `0.0` for anything unparsable.
pub fn parse_float(value: &str) -> f64 {
    value.trim().parse().unwrap_or(0.0)
}

/// Interprets a yes/no answer. An empty or unrecognised answer yields `default`.
pub fn parse_confirm(value: &str, default: bool) -> bool {
    match value.trim().to_ascii_lowercase().as_str() {
        "" => default,
        "y" | "yes" => true,
        "n" | "no" => false,
        _ => default,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn confirm_variants() {
        assert!(parse_confirm("y", false));
        assert!(parse_confirm("YES", false));
        assert!(!parse_confirm("n", true));
        assert!(parse_confirm("", true));
        assert!(!parse_confirm("maybe", false));
    }

    #[test]
    fn scalar_parsing() {
        assert_eq!(parse_int(" 42 "), 42);
        assert_eq!(parse_int("nope"), 0);
        assert_eq!(parse_float("2.5"), 2.5);
        assert_eq!(parse_float("x"), 0.0);
    }
}
