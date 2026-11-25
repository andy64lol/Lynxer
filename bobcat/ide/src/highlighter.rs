//! Lynxer syntax highlighting for iced's `text_editor`.
//!
//! Ported from the old web IDE's Monarch grammar, then extended with builtins,
//! function calls, constants, doc comments, multi-character operators, numeric
//! literals and attributes. Implements iced's `Highlighter` trait directly, so
//! no syntect/sublime-syntax files are needed.

use std::ops::Range;

use iced::advanced::text::highlighter::{Format, Highlighter};

use crate::completions::BUILTINS;

pub const KEYWORDS: &[&str] = &[
    "fn", "global", "func", "local", "lambda", "template", "build", "named",
    "extending", "class", "extends", "struct", "enum", "setup", "main", "return",
    "if", "elif", "else", "while", "for", "doWhile", "iterate", "forever",
    "switch", "case", "default", "try", "catch", "raise", "break", "continue",
    "restart", "new", "this", "super", "import", "importAs", "export", "const",
    "shared", "type", "of", "await", "exec", "codeblock", "inter", "true",
    "false", "none", "and", "or", "not", "xor", "is", "isnt",
];

pub const TYPES: &[&str] = &[
    "any", "int", "float", "num", "char", "str", "bool", "numBool", "bit",
    "void", "array", "map", "set", "tuple", "ptr", "ref",
];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Token {
    Keyword,
    Type,
    Builtin,
    Function,
    Constant,
    Attribute,
    String,
    Number,
    Comment,
    DocComment,
    Operator,
    Delimiter,
    Bracket,
}

impl Token {
    fn color(self) -> iced::Color {
        match self {
            Token::Keyword => iced::Color::from_rgb8(0x56, 0x9c, 0xd6),
            Token::Type => iced::Color::from_rgb8(0x4e, 0xc9, 0xb0),
            Token::Builtin => iced::Color::from_rgb8(0xdc, 0xdc, 0xaa),
            Token::Function => iced::Color::from_rgb8(0xdc, 0xdc, 0xaa),
            Token::Constant => iced::Color::from_rgb8(0x56, 0x9c, 0xd6),
            Token::Attribute => iced::Color::from_rgb8(0xc5, 0x86, 0xc0),
            Token::String => iced::Color::from_rgb8(0xce, 0x91, 0x78),
            Token::Number => iced::Color::from_rgb8(0xb5, 0xce, 0xa8),
            Token::Comment => iced::Color::from_rgb8(0x6a, 0x99, 0x55),
            Token::DocComment => iced::Color::from_rgb8(0x60, 0x8b, 0x4e),
            Token::Operator => iced::Color::from_rgb8(0xd4, 0xd4, 0xd4),
            Token::Delimiter => iced::Color::from_rgb8(0xd4, 0xd4, 0xd4),
            Token::Bracket => iced::Color::from_rgb8(0xff, 0xd7, 0x00),
        }
    }
}

/// Function pointer passed to `TextEditor::highlight_with`.
pub fn to_format(token: &Token, _theme: &iced::Theme) -> Format<iced::Font> {
    Format {
        color: Some(token.color()),
        font: None,
    }
}

pub struct LynxerHighlighter {
    line: usize,
}

impl Highlighter for LynxerHighlighter {
    type Settings = ();
    type Highlight = Token;
    type Iterator<'a> = std::vec::IntoIter<(Range<usize>, Token)>;

    fn new(_settings: &Self::Settings) -> Self {
        Self { line: 0 }
    }

    fn update(&mut self, _new_settings: &Self::Settings) {}

    fn change_line(&mut self, line: usize) {
        self.line = line;
    }

    fn highlight_line(&mut self, line: &str) -> Self::Iterator<'_> {
        let tokens = tokenize(line);
        self.line += 1;
        tokens.into_iter()
    }

    fn current_line(&self) -> usize {
        self.line
    }
}

fn is_ident(c: u8) -> bool {
    c.is_ascii_alphanumeric() || c == b'_'
}

/// Length of a two-byte operator starting at `i`, if any.
fn two_byte_operator(b: &[u8], i: usize) -> Option<usize> {
    if i + 1 >= b.len() {
        return None;
    }
    let pair = (b[i], b[i + 1]);
    if matches!(
        pair,
        (b'-', b'>')
            | (b'=', b'>')
            | (b'=', b'=')
            | (b'!', b'=')
            | (b'<', b'=')
            | (b'>', b'=')
            | (b'+', b'=')
            | (b'-', b'=')
            | (b'*', b'=')
            | (b'/', b'=')
            | (b'%', b'=')
            | (b'&', b'&')
            | (b'|', b'|')
            | (b':', b':')
            | (b'+', b'+')
            | (b'-', b'-')
            | (b'.', b'.')
    ) {
        Some(2)
    } else {
        None
    }
}

fn tokenize(line: &str) -> Vec<(Range<usize>, Token)> {
    let b = line.as_bytes();
    let mut out = Vec::new();
    let mut i = 0usize;

    while i < b.len() {
        let c = b[i];

        // Doc comments: ## ... and /// ...
        if (c == b'#' && i + 1 < b.len() && b[i + 1] == b'#')
            || (c == b'/' && i + 2 < b.len() && b[i + 1] == b'/' && b[i + 2] == b'/')
        {
            out.push((i..b.len(), Token::DocComment));
            break;
        }

        // Line comments: # ... and // ...
        if c == b'#' || (c == b'/' && i + 1 < b.len() && b[i + 1] == b'/') {
            out.push((i..b.len(), Token::Comment));
            break;
        }

        // Block comment /* ... */ (single line).
        if c == b'/' && i + 1 < b.len() && b[i + 1] == b'*' {
            let start = i;
            i += 2;
            while i + 1 < b.len() && !(b[i] == b'*' && b[i + 1] == b'/') {
                i += 1;
            }
            i = (i + 2).min(b.len());
            out.push((start..i, Token::Comment));
            continue;
        }

        // Strings with escapes.
        if c == b'"' || c == b'\'' {
            let quote = c;
            let start = i;
            i += 1;
            while i < b.len() {
                if b[i] == b'\\' {
                    i = (i + 2).min(b.len());
                    continue;
                }
                if b[i] == quote {
                    i += 1;
                    break;
                }
                i += 1;
            }
            out.push((start..i.min(b.len()), Token::String));
            continue;
        }

        // Attributes: @name
        if c == b'@' && i + 1 < b.len() && (b[i + 1].is_ascii_alphabetic() || b[i + 1] == b'_') {
            let start = i;
            i += 1;
            while i < b.len() && is_ident(b[i]) {
                i += 1;
            }
            out.push((start..i, Token::Attribute));
            continue;
        }

        // Numbers: hex/binary/octal, decimals, floats and exponents.
        if c.is_ascii_digit() {
            let start = i;
            if c == b'0' && i + 1 < b.len() && matches!(b[i + 1], b'x' | b'X' | b'b' | b'B' | b'o' | b'O') {
                i += 2;
                while i < b.len() && is_ident(b[i]) {
                    i += 1;
                }
            } else {
                while i < b.len() && (b[i].is_ascii_digit() || b[i] == b'_') {
                    i += 1;
                }
                if i + 1 < b.len() && b[i] == b'.' && b[i + 1].is_ascii_digit() {
                    i += 1;
                    while i < b.len() && (b[i].is_ascii_digit() || b[i] == b'_') {
                        i += 1;
                    }
                }
                if i < b.len() && matches!(b[i], b'e' | b'E') {
                    let mut j = i + 1;
                    if j < b.len() && matches!(b[j], b'+' | b'-') {
                        j += 1;
                    }
                    if j < b.len() && b[j].is_ascii_digit() {
                        i = j + 1;
                        while i < b.len() && b[i].is_ascii_digit() {
                            i += 1;
                        }
                    }
                }
            }
            out.push((start..i, Token::Number));
            continue;
        }

        // Identifiers: keywords, types, builtins, constants, function calls.
        if c.is_ascii_alphabetic() || c == b'_' {
            let start = i;
            while i < b.len() && is_ident(b[i]) {
                i += 1;
            }
            let word = &line[start..i];
            let token = if KEYWORDS.contains(&word) {
                if matches!(word, "true" | "false" | "none") {
                    Some(Token::Constant)
                } else {
                    Some(Token::Keyword)
                }
            } else if TYPES.contains(&word) {
                Some(Token::Type)
            } else if BUILTINS.contains(&word) {
                Some(Token::Builtin)
            } else {
                let mut j = i;
                while j < b.len() && b[j] == b' ' {
                    j += 1;
                }
                if j < b.len() && b[j] == b'(' {
                    Some(Token::Function)
                } else {
                    None
                }
            };
            if let Some(t) = token {
                out.push((start..i, t));
            }
            continue;
        }

        // Operators (grouped), delimiters, brackets.
        let len = two_byte_operator(b, i).unwrap_or(1);
        let token = match c {
            b'{' | b'}' | b'(' | b')' | b'[' | b']' => Some(Token::Bracket),
            b';' | b',' | b'.' => Some(Token::Delimiter),
            b'+' | b'-' | b'*' | b'/' | b'%' | b'=' | b'&' | b'|' | b'<'
            | b'>' | b'!' | b'^' | b'~' | b':' | b'?' => Some(Token::Operator),
            _ => None,
        };
        if let Some(t) = token {
            out.push((i..i + len, t));
        }
        i += len;
    }

    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn token_kinds_cover_the_language() {
        let kinds: Vec<Token> = tokenize("fn main() -> int { return 42; } # done")
            .into_iter()
            .map(|(_, kind)| kind)
            .collect();
        assert!(kinds.contains(&Token::Keyword));
        assert!(kinds.contains(&Token::Type));
        assert!(kinds.contains(&Token::Number));
        assert!(kinds.contains(&Token::Comment));
        assert!(kinds.contains(&Token::Bracket));
        assert!(kinds.contains(&Token::Operator));
    }

    #[test]
    fn richer_tokens_are_recognised() {
        let kinds: Vec<Token> = tokenize("@decorator println(true) 0xFF 1.5e3 == /// docs")
            .into_iter()
            .map(|(_, kind)| kind)
            .collect();
        assert!(kinds.contains(&Token::Attribute));
        assert!(kinds.contains(&Token::Builtin));
        assert!(kinds.contains(&Token::Constant));
        assert!(kinds.contains(&Token::Number));
        assert!(kinds.contains(&Token::DocComment));
        let calls: Vec<Token> = tokenize("myHelper(1)")
            .into_iter()
            .map(|(_, kind)| kind)
            .collect();
        assert!(calls.contains(&Token::Function));
    }

    #[test]
    fn multi_char_operators_are_one_token() {
        let tokens = tokenize("a -> b == c");
        assert!(tokens.iter().any(|(r, t)| *t == Token::Operator && r.len() == 2));
    }

    #[test]
    fn ranges_are_ordered_in_bounds_and_on_char_boundaries() {
        let samples = [
            "# comment",
            "## doc comment",
            "// comment",
            "/// doc",
            "/* block */ tail",
            "\"a string with é\"",
            "'c'",
            "x = 3.14 + y",
            "fn main() { }",
            "/* unterminated",
            "λ + 1",
            "@attr 0xFF == done",
        ];
        for sample in samples {
            let mut last = 0usize;
            for (range, _) in tokenize(sample) {
                assert!(range.start >= last, "overlapping range in {sample:?}");
                assert!(range.end <= sample.len(), "out of bounds in {sample:?}");
                assert!(
                    sample.is_char_boundary(range.start) && sample.is_char_boundary(range.end),
                    "range is not a char boundary in {sample:?}"
                );
                last = range.end;
            }
        }
    }

    #[test]
    fn highlighter_tracks_lines() {
        let mut h = LynxerHighlighter::new(&());
        assert_eq!(h.current_line(), 0);
        h.highlight_line("fn main() {}");
        assert_eq!(h.current_line(), 1);
        h.change_line(0);
        assert_eq!(h.current_line(), 0);
    }
}
