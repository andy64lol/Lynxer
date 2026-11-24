//! Wynxer lexer: turns source text into a stream of tokens.

#[derive(Debug, Clone, PartialEq)]
pub enum TokenKind {
    // Keywords.
    Fn, If, Elif, Else, While, For, Return, Import, Export, True, False,
    // Types.
    Int, Float, Str, Bool, Void, List, Map,
    // Identifiers and literals.
    Ident(String),
    IntLit(i64),
    FloatLit(f64),
    StrLit(String),
    // Operators.
    Plus, Minus, Star, Slash, Percent, Eq, EqEq, Bang, BangEq, Lt, LtEq,
    Gt, GtEq, And, Or, Arrow,
    // Punctuation.
    LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semicolon,
    Colon, Dot, Assign,
    // End of input.
    Eof,
}

#[derive(Debug, Clone)]
pub struct Token {
    pub kind: TokenKind,
    pub line: usize,
    pub column: usize,
}

pub struct Lexer {
    source: Vec<char>,
    pos: usize,
    line: usize,
    column: usize,
}

impl Lexer {
    pub fn new(source: &str) -> Self {
        Self { source: source.chars().collect(), pos: 0, line: 1, column: 1 }
    }

    pub fn tokenize(&mut self) -> Result<Vec<Token>, String> {
        let mut tokens = Vec::new();
        while self.pos < self.source.len() {
            self.skip_whitespace_and_comments();
            if self.pos >= self.source.len() {
                break;
            }
            let tok = self.next_token()?;
            tokens.push(tok);
        }
        tokens.push(Token { kind: TokenKind::Eof, line: self.line, column: self.column });
        Ok(tokens)
    }

    fn skip_whitespace_and_comments(&mut self) {
        while self.pos < self.source.len() {
            let c = self.source[self.pos];
            if c == ' ' || c == '\t' || c == '\r' || c == '\n' {
                if c == '\n' { self.line += 1; self.column = 1; } else { self.column += 1; }
                self.pos += 1;
            } else if c == '/' && self.peek(1) == Some('/') {
                // Line comment.
                while self.pos < self.source.len() && self.source[self.pos] != '\n' {
                    self.pos += 1;
                }
            } else if c == '/' && self.peek(1) == Some('*') {
                // Block comment.
                self.pos += 2;
                self.column += 2;
                while self.pos + 1 < self.source.len() {
                    if self.source[self.pos] == '*' && self.source[self.pos + 1] == '/' {
                        self.pos += 2;
                        self.column += 2;
                        break;
                    }
                    if self.source[self.pos] == '\n' { self.line += 1; self.column = 1; }
                    else { self.column += 1; }
                    self.pos += 1;
                }
            } else {
                break;
            }
        }
    }

    fn peek(&self, offset: usize) -> Option<char> {
        self.source.get(self.pos + offset).copied()
    }

    fn next_token(&mut self) -> Result<Token, String> {
        let start_line = self.line;
        let start_column = self.column;
        let c = self.source[self.pos];
        // Identifiers and keywords.
        if c.is_alphabetic() || c == '_' {
            let mut ident = String::new();
            while self.pos < self.source.len() && (self.source[self.pos].is_alphanumeric() || self.source[self.pos] == '_') {
                ident.push(self.source[self.pos]);
                self.pos += 1;
                self.column += 1;
            }
            let kind = match ident.as_str() {
                "fn" => TokenKind::Fn,
                "if" => TokenKind::If,
                "elif" => TokenKind::Elif,
                "else" => TokenKind::Else,
                "while" => TokenKind::While,
                "for" => TokenKind::For,
                "return" => TokenKind::Return,
                "import" => TokenKind::Import,
                "export" => TokenKind::Export,
                "true" => TokenKind::True,
                "false" => TokenKind::False,
                "int" => TokenKind::Int,
                "float" => TokenKind::Float,
                "str" => TokenKind::Str,
                "bool" => TokenKind::Bool,
                "void" => TokenKind::Void,
                "list" => TokenKind::List,
                "map" => TokenKind::Map,
                _ => TokenKind::Ident(ident),
            };
            return Ok(Token { kind, line: start_line, column: start_column });
        }
        // Numbers.
        if c.is_ascii_digit() {
            let mut number = String::new();
            while self.pos < self.source.len() && (self.source[self.pos].is_ascii_digit() || self.source[self.pos] == '.') {
                number.push(self.source[self.pos]);
                self.pos += 1;
                self.column += 1;
            }
            if number.contains('.') {
                return Ok(Token {
                    kind: TokenKind::FloatLit(number.parse().unwrap_or(0.0)),
                    line: start_line,
                    column: start_column,
                });
            }
            return Ok(Token {
                kind: TokenKind::IntLit(number.parse().unwrap_or(0)),
                line: start_line,
                column: start_column,
            });
        }
        // Strings.
        if c == '"' {
            self.pos += 1;
            self.column += 1;
            let mut s = String::new();
            while self.pos < self.source.len() && self.source[self.pos] != '"' {
                if self.source[self.pos] == '\\' {
                    self.pos += 1;
                    self.column += 1;
                    if self.pos < self.source.len() {
                        let esc = self.source[self.pos];
                        match esc {
                            'n' => s.push('\n'),
                            't' => s.push('\t'),
                            'r' => s.push('\r'),
                            '\\' => s.push('\\'),
                            '"' => s.push('"'),
                            _ => s.push(esc),
                        }
                        self.pos += 1;
                        self.column += 1;
                    }
                } else {
                    if self.source[self.pos] == '\n' { self.line += 1; self.column = 1; }
                    else { self.column += 1; }
                    s.push(self.source[self.pos]);
                    self.pos += 1;
                }
            }
            if self.pos < self.source.len() {
                self.pos += 1;
                self.column += 1;
            }
            return Ok(Token { kind: TokenKind::StrLit(s), line: start_line, column: start_column });
        }
        // Two-character operators.
        if c == '=' && self.peek(1) == Some('=') {
            self.pos += 2; self.column += 2;
            return Ok(Token { kind: TokenKind::EqEq, line: start_line, column: start_column });
        }
        if c == '!' && self.peek(1) == Some('=') {
            self.pos += 2; self.column += 2;
            return Ok(Token { kind: TokenKind::BangEq, line: start_line, column: start_column });
        }
        if c == '<' && self.peek(1) == Some('=') {
            self.pos += 2; self.column += 2;
            return Ok(Token { kind: TokenKind::LtEq, line: start_line, column: start_column });
        }
        if c == '>' && self.peek(1) == Some('=') {
            self.pos += 2; self.column += 2;
            return Ok(Token { kind: TokenKind::GtEq, line: start_line, column: start_column });
        }
        if c == '&' && self.peek(1) == Some('&') {
            self.pos += 2; self.column += 2;
            return Ok(Token { kind: TokenKind::And, line: start_line, column: start_column });
        }
        if c == '|' && self.peek(1) == Some('|') {
            self.pos += 2; self.column += 2;
            return Ok(Token { kind: TokenKind::Or, line: start_line, column: start_column });
        }
        if c == '-' && self.peek(1) == Some('>') {
            self.pos += 2; self.column += 2;
            return Ok(Token { kind: TokenKind::Arrow, line: start_line, column: start_column });
        }
        // Single-character operators and punctuation.
        self.pos += 1;
        self.column += 1;
        let kind = match c {
            '+' => TokenKind::Plus,
            '-' => TokenKind::Minus,
            '*' => TokenKind::Star,
            '/' => TokenKind::Slash,
            '%' => TokenKind::Percent,
            '=' => TokenKind::Assign,
            '!' => TokenKind::Bang,
            '<' => TokenKind::Lt,
            '>' => TokenKind::Gt,
            '(' => TokenKind::LParen,
            ')' => TokenKind::RParen,
            '{' => TokenKind::LBrace,
            '}' => TokenKind::RBrace,
            '[' => TokenKind::LBracket,
            ']' => TokenKind::RBracket,
            ',' => TokenKind::Comma,
            ';' => TokenKind::Semicolon,
            ':' => TokenKind::Colon,
            '.' => TokenKind::Dot,
            _ => return Err(format!("unexpected character '{}' at {}:{}", c, start_line, start_column)),
        };
        Ok(Token { kind, line: start_line, column: start_column })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_keyword_tokens() {
        let mut lexer = Lexer::new("fn main() -> int { }");
        let tokens = lexer.tokenize().unwrap();
        let kinds: Vec<_> = tokens.iter().map(|t| &t.kind).collect();
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Fn)));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Ident(_))));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Int)));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::LBrace)));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::RBrace)));
    }

    #[test]
    fn test_identifier_token() {
        let mut lexer = Lexer::new("int x = 5;");
        let tokens = lexer.tokenize().unwrap();
        let kinds: Vec<_> = tokens.iter().map(|t| &t.kind).collect();
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Ident(_))));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Int)));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Assign)));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::IntLit(_))));
    }

    #[test]
    fn test_string_token() {
        let mut lexer = Lexer::new(r#"str s = "hello";"#);
        let tokens = lexer.tokenize().unwrap();
        let kinds: Vec<_> = tokens.iter().map(|t| &t.kind).collect();
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::StrLit(_))));
    }

    #[test]
    fn test_operators() {
        let mut lexer = Lexer::new("a = b + c;");

        let tokens = lexer.tokenize().unwrap();
        let kinds: Vec<_> = tokens.iter().map(|t| &t.kind).collect();
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Assign)));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Plus)));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Ident(_))));
    }

    #[test]
    fn test_comments() {
        let mut lexer = Lexer::new("// comment\nfn main() { }");
        let tokens = lexer.tokenize().unwrap();
        let kinds: Vec<_> = tokens.iter().map(|t| &t.kind).collect();
        // fn should still be tokenized after the comment
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Fn)));
    }

    #[test]
    fn test_whitespace_handling() {
        let mut lexer = Lexer::new("  int   x  =  5;");
        let tokens = lexer.tokenize().unwrap();
        let kinds: Vec<_> = tokens.iter().map(|t| &t.kind).collect();
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Int)));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Ident(_))));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::Assign)));
        assert!(kinds.iter().any(|k| matches!(k, TokenKind::IntLit(_))));
    }

    #[test]
    fn test_eof() {
        let mut lexer = Lexer::new("");
        let tokens = lexer.tokenize().unwrap();
        assert_eq!(tokens.len(), 1);
        assert!(matches!(tokens[0].kind, TokenKind::Eof));
    }

    #[test]
    fn test_malformed_input() {
        let mut lexer = Lexer::new("\x00");
        let result = lexer.tokenize();
        assert!(result.is_err());
    }
}
