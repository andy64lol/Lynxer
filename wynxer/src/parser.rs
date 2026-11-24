//! Wynxer parser: recursive descent over the token stream.
//!
//! Wynxer uses Lynxer-style typed declarations (`int x = 5;`, never `let`),
//! `fn` functions with typed params/return, control flow (if/elif/else,
//! while, for), import/export, structs, enums, and macros with `pub`,
//! `*args` variadics and codeblock parameters.

use crate::ast::{Expr, Function, MacroDef, Module, Stmt};
use crate::lexer::{Lexer, Token, TokenKind};

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn new(source: &str) -> Result<Self, String> {
        let mut lexer = Lexer::new(source);
        let tokens = lexer.tokenize()?;
        Ok(Self { tokens, pos: 0 })
    }

    pub fn parse(&mut self) -> Result<Module, String> {
        let mut module = Module {
            name: None,
            functions: Vec::new(),
            structs: Vec::new(),
            enums: Vec::new(),
            imports: Vec::new(),
            exports: Vec::new(),
            macros: Vec::new(),
        };
        while !self.at(TokenKind::Eof) {
            // Optional `pub` modifier applies to the next function/macro.
            let pub_ = if self.check_ident("pub") {
                self.advance();
                true
            } else {
                false
            };

            if self.match_kind(TokenKind::Import) {
                // import("dom");  /  import "dom";  /  import dom;
                if self.check(TokenKind::LParen) {
                    self.advance();
                    let name = self.parse_import_name()?;
                    self.expect(TokenKind::RParen)?;
                    self.expect(TokenKind::Semicolon)?;
                    module.imports.push(name);
                } else {
                    let name = self.parse_import_name()?;
                    self.expect(TokenKind::Semicolon)?;
                    module.imports.push(name);
                }
                continue;
            }
            if self.match_kind(TokenKind::Export) {
                let name = self.expect_ident()?;
                self.expect(TokenKind::Semicolon)?;
                module.exports.push(name);
                continue;
            }
            if self.check_ident("struct") {
                self.advance();
                let s = self.parse_struct()?;
                module.structs.push(s);
                continue;
            }
            if self.check_ident("enum") {
                self.advance();
                let e = self.parse_enum()?;
                module.enums.push(e);
                continue;
            }
            if self.check_ident("macro") {
                self.advance();
                let m = self.parse_macro(pub_)?;
                module.macros.push(m);
                continue;
            }
            if self.check(TokenKind::Fn) {
                let f = self.parse_function(pub_)?;
                module.functions.push(f);
                continue;
            }
            // Unknown top-level token: report with location.
            let tok = self.current();
            return Err(format!("unexpected token at {}:{}", tok.line, tok.column));
        }
        Ok(module)
    }

    fn parse_import_name(&mut self) -> Result<String, String> {
        match &self.current().kind {
            TokenKind::StrLit(s) => {
                let s = s.clone();
                self.advance();
                Ok(s)
            }
            TokenKind::Ident(_) => self.expect_ident(),
            _ => {
                let tok = self.current();
                Err(format!("expected module name at {}:{}", tok.line, tok.column))
            }
        }
    }

    fn parse_struct(&mut self) -> Result<crate::ast::StructDef, String> {
        let name = self.expect_ident()?;
        self.expect(TokenKind::LBrace)?;
        let mut fields = Vec::new();
        while !self.check(TokenKind::RBrace) && !self.at(TokenKind::Eof) {
            let fname = self.expect_ident()?;
            let fty = if self.match_kind(TokenKind::Colon) {
                self.expect_type_name()?
            } else {
                "any".to_string()
            };
            fields.push((fname, fty));
            // Optional trailing semicolon or comma separator.
            if self.check(TokenKind::Semicolon) {
                self.advance();
            }
            if !self.match_kind(TokenKind::Comma) {
                break;
            }
        }
        self.expect(TokenKind::RBrace)?;
        Ok(crate::ast::StructDef { name, fields })
    }

    fn parse_enum(&mut self) -> Result<crate::ast::EnumDef, String> {
        let name = self.expect_ident()?;
        let mut variants = Vec::new();
        if self.match_kind(TokenKind::Assign) {
            self.expect(TokenKind::LBracket)?;
            if !self.check(TokenKind::RBracket) {
                loop {
                    variants.push(self.expect_ident()?);
                    if !self.match_kind(TokenKind::Comma) {
                        break;
                    }
                }
            }
            self.expect(TokenKind::RBracket)?;
            self.expect(TokenKind::Semicolon)?;
        } else if self.check(TokenKind::LBrace) {
            self.advance();
            while !self.check(TokenKind::RBrace) && !self.at(TokenKind::Eof) {
                variants.push(self.expect_ident()?);
                if !self.match_kind(TokenKind::Comma) {
                    break;
                }
            }
            self.expect(TokenKind::RBrace)?;
            if self.check(TokenKind::Semicolon) {
                self.advance();
            }
        }
        Ok(crate::ast::EnumDef { name, variants })
    }

    /// True when the current token sequence is `*args` (`Star` + `Ident("args")`).
    fn check_varargs(&self) -> bool {
        if !self.check(TokenKind::Star) {
            return false;
        }
        matches!(
            self.tokens.get(self.pos + 1).map(|t| &t.kind),
            Some(TokenKind::Ident(n)) if n == "args"
        )
    }

    /// Parse a macro: `macro name!(a, *args, body_block) { ... }`
    /// Supports `pub` (visibility), `*args` (variadic) and codeblock params
    /// (a parameter typed as `code` or named `block`).
    fn parse_macro(&mut self, pub_: bool) -> Result<MacroDef, String> {
        let name = self.expect_ident()?;
        let mut params = Vec::new();
        let mut varargs = false;
        if self.check(TokenKind::Bang) {
            self.advance();
        }
        if self.match_kind(TokenKind::LParen) {
            while !self.check(TokenKind::RParen) && !self.at(TokenKind::Eof) {
                // Variadic rest marker: `...` (three Dot tokens).
                if self.check(TokenKind::Dot) {
                    varargs = true;
                    while self.check(TokenKind::Dot) {
                        self.advance();
                    }
                    params.push("*args".to_string());
                    break;
                }
                // Variadic rest marker: `*args`.
                if self.check_varargs() {
                    varargs = true;
                    self.advance(); // `*`
                    self.advance(); // `args`
                    params.push("*args".to_string());
                    if self.check(TokenKind::Comma) {
                        self.advance();
                    }
                    continue;
                }
                let p = self.expect_ident()?;
                // A codeblock parameter is `name: code` or `name: block`.
                if self.match_kind(TokenKind::Colon) {
                    let ty = self.expect_type_name()?;
                    if ty == "code" || ty == "block" {
                        params.push(format!("{}: codeblock", p));
                    } else {
                        params.push(p);
                    }
                } else {
                    params.push(p);
                }
                if !self.match_kind(TokenKind::Comma) {
                    break;
                }
            }
            self.expect(TokenKind::RParen)?;
        }
        let body = if self.check(TokenKind::LBrace) {
            self.advance();
            self.parse_block()?
        } else {
            Vec::new()
        };
        let mut def = MacroDef {
            pub_,
            name,
            params,
            body,
        };
        // Encode variadic marker in params so callers can inspect it.
        if varargs && !def.params.iter().any(|p| p == "*args") {
            def.params.push("*args".to_string());
        }
        Ok(def)
    }

    fn parse_function(&mut self, pub_: bool) -> Result<Function, String> {
        self.expect(TokenKind::Fn)?;
        let name = self.expect_ident()?;
        let mut params = Vec::new();
        let mut varargs = false;
        self.expect(TokenKind::LParen)?;
        while !self.check(TokenKind::RParen) && !self.at(TokenKind::Eof) {
            if self.check_varargs() {
                varargs = true;
                self.advance(); // `*`
                self.advance(); // `args`
                if self.check(TokenKind::Comma) {
                    self.advance();
                }
                continue;
            }
            let pname = self.expect_ident()?;
            let pty = if self.match_kind(TokenKind::Colon) {
                self.expect_type_name()?
            } else {
                "any".to_string()
            };
            params.push((pname, pty));
            if !self.match_kind(TokenKind::Comma) {
                break;
            }
        }
        self.expect(TokenKind::RParen)?;
        let return_type = if self.match_kind(TokenKind::Arrow) {
            Some(self.expect_type_name()?)
        } else {
            None
        };
        self.expect(TokenKind::LBrace)?;
        let body = self.parse_block()?;
        Ok(Function {
            name,
            pub_,
            params,
            varargs,
            return_type,
            body,
            is_async: false,
            is_exported: false,
        })
    }

    fn parse_block(&mut self) -> Result<Vec<Stmt>, String> {
        let mut stmts = Vec::new();
        while !self.check(TokenKind::RBrace) && !self.at(TokenKind::Eof) {
            stmts.push(self.parse_stmt()?);
        }
        self.expect(TokenKind::RBrace)?;
        Ok(stmts)
    }

    fn parse_stmt(&mut self) -> Result<Stmt, String> {
        // Assignment: `x = 5;`
        if let TokenKind::Ident(ref name) = self.current().kind {
            let name = name.clone();
            if matches!(
                self.tokens.get(self.pos + 1).map(|t| &t.kind),
                Some(TokenKind::Assign)
            ) {
                self.advance();
                self.advance();
                let value = self.parse_expr()?;
                self.expect(TokenKind::Semicolon)?;
                return Ok(Stmt::Assign { name, value });
            }
        }
        // Typed variable declaration: `int x = 5;` (no `let`).
        if self.is_type_keyword() {
            let ty = self.expect_type_name()?;
            let name = self.expect_ident()?;
            self.expect(TokenKind::Assign)?;
            let value = self.parse_expr()?;
            self.expect(TokenKind::Semicolon)?;
            return Ok(Stmt::Var { ty, name, value });
        }
        // if / elif / else
        if self.match_kind(TokenKind::If) {
            self.expect(TokenKind::LParen)?;
            let cond = self.parse_expr()?;
            self.expect(TokenKind::RParen)?;
            self.expect(TokenKind::LBrace)?;
            let then_branch = self.parse_block()?;
            let mut else_branch = Vec::new();
            if self.match_kind(TokenKind::Elif) {
                self.expect(TokenKind::LParen)?;
                let else_cond = self.parse_expr()?;
                self.expect(TokenKind::RParen)?;
                self.expect(TokenKind::LBrace)?;
                let elif_body = self.parse_block()?;
                else_branch.push(Stmt::If {
                    cond: else_cond,
                    then_branch: elif_body,
                    else_branch: Vec::new(),
                });
            } else if self.match_kind(TokenKind::Else) {
                self.expect(TokenKind::LBrace)?;
                else_branch = self.parse_block()?;
            }
            return Ok(Stmt::If { cond, then_branch, else_branch });
        }
        // while
        if self.match_kind(TokenKind::While) {
            self.expect(TokenKind::LParen)?;
            let cond = self.parse_expr()?;
            self.expect(TokenKind::RParen)?;
            self.expect(TokenKind::LBrace)?;
            let body = self.parse_block()?;
            return Ok(Stmt::While { cond, body });
        }
        // for (with typed init, condition, assignment step)
        if self.match_kind(TokenKind::For) {
            self.expect(TokenKind::LParen)?;
            let init = if self.check(TokenKind::Semicolon) {
                None
            } else if self.is_type_keyword() {
                let ty = self.expect_type_name()?;
                let name = self.expect_ident()?;
                self.expect(TokenKind::Assign)?;
                let value = self.parse_expr()?;
                Some(Box::new(Stmt::Var { ty, name, value }))
            } else {
                let e = self.parse_expr()?;
                Some(Box::new(Stmt::Expr(e)))
            };
            self.expect(TokenKind::Semicolon)?;
            let cond = if self.check(TokenKind::Semicolon) {
                None
            } else {
                Some(self.parse_expr()?)
            };
            self.expect(TokenKind::Semicolon)?;
            let step = if self.check(TokenKind::RParen) {
                None
            } else {
                Some(Box::new(self.parse_stmt_no_semi()?))
            };
            self.expect(TokenKind::RParen)?;
            self.expect(TokenKind::LBrace)?;
            let body = self.parse_block()?;
            return Ok(Stmt::For { init, cond, step, body });
        }
        // return
        if self.match_kind(TokenKind::Return) {
            let value = if self.check(TokenKind::Semicolon) {
                Expr::None
            } else {
                self.parse_expr()?
            };
            self.expect(TokenKind::Semicolon)?;
            return Ok(Stmt::Return(value));
        }
        // raise
        if self.check_ident("raise") {
            self.advance();
            let value = self.parse_expr()?;
            self.expect(TokenKind::Semicolon)?;
            return Ok(Stmt::Raise(value));
        }
        // nested block
        if self.check(TokenKind::LBrace) {
            return Ok(Stmt::Block(self.parse_block()?));
        }
        // expression statement
        let e = self.parse_expr()?;
        self.expect(TokenKind::Semicolon)?;
        Ok(Stmt::Expr(e))
    }

    /// Used for a for-loop step: parse an expression or assignment without
    /// requiring a trailing semicolon.
    fn parse_stmt_no_semi(&mut self) -> Result<Stmt, String> {
        if let TokenKind::Ident(ref name) = self.current().kind {
            let name = name.clone();
            if matches!(
                self.tokens.get(self.pos + 1).map(|t| &t.kind),
                Some(TokenKind::Assign)
            ) {
                self.advance();
                self.advance();
                let value = self.parse_expr()?;
                return Ok(Stmt::Assign { name, value });
            }
        }
        Ok(Stmt::Expr(self.parse_expr()?))
    }

    // ---- expression parsing (precedence chain) ----

    fn parse_expr(&mut self) -> Result<Expr, String> {
        self.parse_assignment()
    }

    fn parse_assignment(&mut self) -> Result<Expr, String> {
        let left = self.parse_ternary()?;
        if self.check(TokenKind::Assign) {
            self.advance();
            let _right = self.parse_assignment()?;
            // Assignment as expression is simplified; keep the left side.
            return Ok(left);
        }
        Ok(left)
    }

    fn parse_ternary(&mut self) -> Result<Expr, String> {
        let cond = self.parse_or()?;
        // `a ? b : c` is tokenized as Ident/Bang; simplified: return cond.
        Ok(cond)
    }

    fn parse_or(&mut self) -> Result<Expr, String> {
        let mut left = self.parse_and()?;
        while self.check(TokenKind::Or) {
            let op = self.current().kind.clone();
            self.advance();
            let right = self.parse_and()?;
            left = Expr::Binary {
                op: format!("{:?}", op),
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_and(&mut self) -> Result<Expr, String> {
        let mut left = self.parse_equality()?;
        while self.check(TokenKind::And) {
            let op = self.current().kind.clone();
            self.advance();
            let right = self.parse_equality()?;
            left = Expr::Binary {
                op: format!("{:?}", op),
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_equality(&mut self) -> Result<Expr, String> {
        let mut left = self.parse_comparison()?;
        while self.check(TokenKind::EqEq) || self.check(TokenKind::BangEq) {
            let op = self.current().kind.clone();
            self.advance();
            let right = self.parse_comparison()?;
            left = Expr::Binary {
                op: format!("{:?}", op),
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_comparison(&mut self) -> Result<Expr, String> {
        let mut left = self.parse_term()?;
        while self.check(TokenKind::Lt)
            || self.check(TokenKind::Gt)
            || self.check(TokenKind::LtEq)
            || self.check(TokenKind::GtEq)
        {
            let op = self.current().kind.clone();
            self.advance();
            let right = self.parse_term()?;
            left = Expr::Binary {
                op: format!("{:?}", op),
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_term(&mut self) -> Result<Expr, String> {
        let mut left = self.parse_factor()?;
        while self.check(TokenKind::Plus) || self.check(TokenKind::Minus) {
            let op = self.current().kind.clone();
            self.advance();
            let right = self.parse_factor()?;
            left = Expr::Binary {
                op: format!("{:?}", op),
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_factor(&mut self) -> Result<Expr, String> {
        let mut left = self.parse_unary()?;
        while self.check(TokenKind::Star)
            || self.check(TokenKind::Slash)
            || self.check(TokenKind::Percent)
        {
            let op = self.current().kind.clone();
            self.advance();
            let right = self.parse_unary()?;
            left = Expr::Binary {
                op: format!("{:?}", op),
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_unary(&mut self) -> Result<Expr, String> {
        if self.check(TokenKind::Bang) || self.check(TokenKind::Minus) {
            let op = self.current().kind.clone();
            self.advance();
            let operand = self.parse_unary()?;
            return Ok(Expr::Unary {
                op: format!("{:?}", op),
                operand: Box::new(operand),
            });
        }
        self.parse_postfix()
    }

    fn parse_postfix(&mut self) -> Result<Expr, String> {
        let mut expr = self.parse_primary()?;
        loop {
            // Function call: f(args)
            if self.match_kind(TokenKind::LParen) {
                let mut args = Vec::new();
                while !self.check(TokenKind::RParen) && !self.at(TokenKind::Eof) {
                    args.push(self.parse_expr()?);
                    if !self.match_kind(TokenKind::Comma) {
                        break;
                    }
                }
                self.expect(TokenKind::RParen)?;
                expr = match expr {
                    Expr::Ident(name) => Expr::Call { callee: name, args },
                    other => other,
                };
                continue;
            }
            // Field access: obj.field
            if self.match_kind(TokenKind::Dot) {
                let field = self.expect_ident()?;
                expr = Expr::Field {
                    object: Box::new(expr),
                    field,
                };
                continue;
            }
            // Indexing: obj[index]
            if self.match_kind(TokenKind::LBracket) {
                let index = self.parse_expr()?;
                self.expect(TokenKind::RBracket)?;
                expr = Expr::Index {
                    object: Box::new(expr),
                    index: Box::new(index),
                };
                continue;
            }
            break;
        }
        Ok(expr)
    }

    fn parse_primary(&mut self) -> Result<Expr, String> {
        if self.match_kind(TokenKind::LParen) {
            let e = self.parse_expr()?;
            self.expect(TokenKind::RParen)?;
            return Ok(e);
        }
        if let TokenKind::IntLit(n) = self.current().kind {
            self.advance();
            return Ok(Expr::Int(n));
        }
        if let TokenKind::FloatLit(f) = self.current().kind {
            self.advance();
            return Ok(Expr::Float(f));
        }
        if let TokenKind::StrLit(ref s) = self.current().kind {
            let s = s.clone();
            self.advance();
            return Ok(Expr::Str(s));
        }
        if let TokenKind::Ident(ref name) = self.current().kind {
            let name = name.clone();
            self.advance();
            return Ok(Expr::Ident(name));
        }
        if self.match_kind(TokenKind::True) {
            return Ok(Expr::Bool(true));
        }
        if self.match_kind(TokenKind::False) {
            return Ok(Expr::Bool(false));
        }
        // `none` / `null` literal as identifier
        if self.check_ident("none") || self.check_ident("null") {
            self.advance();
            return Ok(Expr::None);
        }
        let tok = self.current();
        Err(format!("unexpected token in expression at {}:{}", tok.line, tok.column))
    }

    // ---- helpers ----

    fn current(&self) -> &Token {
        self.tokens
            .get(self.pos)
            .unwrap_or(&self.tokens[self.tokens.len() - 1])
    }

    fn check(&self, kind: TokenKind) -> bool {
        std::mem::discriminant(&self.current().kind) == std::mem::discriminant(&kind)
    }

    fn at(&self, kind: TokenKind) -> bool {
        std::mem::discriminant(&self.current().kind) == std::mem::discriminant(&kind)
    }

    fn match_kind(&mut self, kind: TokenKind) -> bool {
        if self.check(kind.clone()) {
            self.advance();
            true
        } else {
            false
        }
    }

    fn check_ident(&self, name: &str) -> bool {
        match &self.current().kind {
            TokenKind::Ident(n) => n == name,
            _ => false,
        }
    }

    fn advance(&mut self) {
        if self.pos < self.tokens.len() {
            self.pos += 1;
        }
    }

    fn expect(&mut self, kind: TokenKind) -> Result<(), String> {
        if self.check(kind.clone()) {
            self.advance();
            Ok(())
        } else {
            let tok = self.current();
            Err(format!("expected {:?} at {}:{}", kind, tok.line, tok.column))
        }
    }

    fn expect_ident(&mut self) -> Result<String, String> {
        if let TokenKind::Ident(name) = &self.current().kind {
            let name = name.clone();
            self.advance();
            Ok(name)
        } else {
            let tok = self.current();
            Err(format!("expected identifier at {}:{}", tok.line, tok.column))
        }
    }

    fn expect_type_name(&mut self) -> Result<String, String> {
        let tok = self.current();
        let name = match &tok.kind {
            TokenKind::Int => "int".to_string(),
            TokenKind::Float => "float".to_string(),
            TokenKind::Str => "str".to_string(),
            TokenKind::Bool => "bool".to_string(),
            TokenKind::Void => "void".to_string(),
            TokenKind::List => "list".to_string(),
            TokenKind::Map => "map".to_string(),
            TokenKind::Ident(n)
                if n == "none"
                    || n == "null"
                    || n == "None"
                    || n == "code"
                    || n == "block" =>
            {
                n.clone()
            }
            _ => {
                return Err(format!("expected type name at {}:{}", tok.line, tok.column));
            }
        };
        self.advance();
        Ok(name)
    }

    fn is_type_keyword(&self) -> bool {
        matches!(
            self.current().kind,
            TokenKind::Int
                | TokenKind::Float
                | TokenKind::Str
                | TokenKind::Bool
                | TokenKind::Void
                | TokenKind::List
                | TokenKind::Map
        )
    }
}

/// Convenience: parse a Wynxer source string into a module.
pub fn parse_module(source: &str) -> Result<Module, String> {
    let mut parser = Parser::new(source)?;
    parser.parse()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_fn_decl() {
        let module = parse_module("fn foo(x: int) -> int { int y = 5; return y; }").unwrap();
        assert_eq!(module.functions.len(), 1);
        let f = &module.functions[0];
        assert_eq!(f.name, "foo");
        assert_eq!(f.params.len(), 1);
        assert_eq!(f.params[0].1, "int");
        assert_eq!(f.return_type.as_deref().unwrap(), "int");
    }

    #[test]
    fn test_parse_if_else() {
        let module = parse_module("fn foo() { if (true) { int x = 1; } else { int y = 2; } }").unwrap();
        assert_eq!(module.functions.len(), 1);
    }

    #[test]
    fn test_parse_while_loop() {
        let module = parse_module("fn foo() { while (cond) { int x = 1; } }").unwrap();
        assert_eq!(module.functions.len(), 1);
    }

    #[test]
    fn test_parse_for_loop() {
        let module =
            parse_module("fn foo() { for (int i = 0; i < 10; i = i + 1) { int x = 1; } }")
                .unwrap();
        assert_eq!(module.functions.len(), 1);
    }

    #[test]
    fn test_parse_return() {
        let module = parse_module("fn foo() -> int { return 5; }").unwrap();
        assert_eq!(module.functions[0].body.len(), 1);
    }

    #[test]
    fn test_parse_import() {
        let module = parse_module("import \"dom\";").unwrap();
        assert_eq!(module.imports, vec!["dom".to_string()]);
    }

    #[test]
    fn test_parse_import_call_form() {
        let module = parse_module("import(\"dom\");").unwrap();
        assert_eq!(module.imports, vec!["dom".to_string()]);
    }

    #[test]
    fn test_parse_export() {
        let module = parse_module("export name;").unwrap();
        assert_eq!(module.exports, vec!["name".to_string()]);
    }

    #[test]
    fn test_parse_struct() {
        let module = parse_module("struct Point { x: int, y: int; }").unwrap();
        assert_eq!(module.structs.len(), 1);
        assert_eq!(module.structs[0].name, "Point");
        assert_eq!(module.structs[0].fields.len(), 2);
    }

    #[test]
    fn test_parse_enum() {
        let module = parse_module("enum Status = [A, B, C];").unwrap();
        assert_eq!(module.enums.len(), 1);
        assert_eq!(module.enums[0].variants.len(), 3);
    }

    #[test]
    fn test_parse_macro_with_varargs_and_codeblock() {
        let module = parse_module("macro repeat!(n, *args, body: code) { }").unwrap();
        assert_eq!(module.macros.len(), 1);
        let m = &module.macros[0];
        assert_eq!(m.name, "repeat");
        assert!(m.params.iter().any(|p| p == "*args"));
        assert!(m.params.iter().any(|p| p.contains("codeblock")));
    }

    #[test]
    fn test_parse_pub_function() {
        let module = parse_module("pub fn main() { int x = 5; }").unwrap();
        assert_eq!(module.functions.len(), 1);
        assert!(module.functions[0].pub_);
    }

    #[test]
    fn test_parse_fn_varargs() {
        let module = parse_module("fn log(*args) { }").unwrap();
        assert_eq!(module.functions.len(), 1);
        assert!(module.functions[0].varargs);
    }

    #[test]
    fn test_full_program() {
        let source = concat!(
            "fn main() -> None { int x = 5; if (x > 0) { int y = x + 1; } else { int z = 0; } return; }\n",
            "import \"dom\";\n",
            "export name;\n",
            "struct Point { x: int, y: int; }\n",
            "enum Status = [A, B];\n",
            "macro foo!(...) { }\n"
        );
        let module = parse_module(source).unwrap();
        assert_eq!(module.functions.len(), 1);
        assert_eq!(module.imports.len(), 1);
        assert_eq!(module.exports.len(), 1);
        assert_eq!(module.structs.len(), 1);
        assert_eq!(module.enums.len(), 1);
        assert_eq!(module.macros.len(), 1);
    }
}
