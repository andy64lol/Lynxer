//! Lint rules for Wynxer scripts and WPF pages.
//!
//! Runs source-location-aware checks and returns structured diagnostics.
//! Rules:
//!   - `no-let`: Wynxer uses typed declarations (`int x = 5;`), never `let`.
//!   - `unused-variable`: a declared local that is never read again.
//!   - `unknown-element`: a `.wpf` element kind not in the 56-kind registry.

use crate::ast::{Expr, Module, Stmt};
use crate::diagnostic::Diagnostic;

/// Lint a Wynxer script source. Returns (diagnostics, had_error).
pub fn lint_script(source: &str, file: Option<&str>) -> (Vec<Diagnostic>, bool) {
    let mut diagnostics = Vec::new();

    // Rule: no `let` keyword anywhere (even in comments is fine to flag, but
    // we only look at token-like occurrences to avoid false positives in
    // strings/comments).
    for (idx, line) in source.lines().enumerate() {
        let trimmed = line.trim_start();
        if trimmed.starts_with("let ") || trimmed.starts_with("let\t") {
            diagnostics.push(
                Diagnostic::warning("Wynxer uses typed declarations (e.g. `int x = 5;`), not `let`")
                    .at(file, idx + 1, line.find("let").unwrap_or(0) + 1),
            );
        }
    }

    match crate::parser::parse_module(source) {
        Ok(module) => {
            diagnostics.extend(lint_module(&module, file));
            (diagnostics, false)
        }
        Err(err) => {
            let (msg, line, column) = crate::diagnostic::extract_location(&err);
            diagnostics.push(Diagnostic::error(msg).at(file, line, column));
            (diagnostics, true)
        }
    }
}

fn lint_module(module: &Module, file: Option<&str>) -> Vec<Diagnostic> {
    let mut diagnostics = Vec::new();
    for func in &module.functions {
        // Collect declared variable names (line 1 of body as a rough location).
        let mut declared: Vec<(String, usize)> = Vec::new();
        for stmt in &func.body {
            collect_declarations(stmt, &mut declared, 0);
        }
        // Count identifier usages in the function body.
        let mut uses: Vec<String> = Vec::new();
        for stmt in &func.body {
            collect_uses(stmt, &mut uses);
        }
        for (name, _line) in &declared {
            if uses.iter().filter(|u| *u == name).count() == 0 {
                diagnostics.push(
                    Diagnostic::warning(format!("variable '{}' is declared but never read", name))
                        .at(file, 0, 0),
                );
            }
        }
    }
    diagnostics
}

fn collect_declarations(stmt: &Stmt, out: &mut Vec<(String, usize)>, _depth: usize) {
    match stmt {
        Stmt::Var { name, .. } => out.push((name.clone(), 0)),
        Stmt::If { then_branch, else_branch, .. } => {
            for s in then_branch {
                collect_declarations(s, out, 0);
            }
            for s in else_branch {
                collect_declarations(s, out, 0);
            }
        }
        Stmt::While { body, .. } | Stmt::ForEach { body, .. } => {
            for s in body {
                collect_declarations(s, out, 0);
            }
        }
        Stmt::For { init, body, .. } => {
            if let Some(init) = init {
                collect_declarations(init, out, 0);
            }
            for s in body {
                collect_declarations(s, out, 0);
            }
        }
        Stmt::Block(stmts) => {
            for s in stmts {
                collect_declarations(s, out, 0);
            }
        }
        _ => {}
    }
}

fn collect_uses(stmt: &Stmt, out: &mut Vec<String>) {
    match stmt {
        Stmt::Var { value, .. } => collect_expr_uses(value, out),
        Stmt::Assign { value, .. } => collect_expr_uses(value, out),
        Stmt::Expr(e) => collect_expr_uses(e, out),
        Stmt::If { cond, then_branch, else_branch } => {
            collect_expr_uses(cond, out);
            for s in then_branch {
                collect_uses(s, out);
            }
            for s in else_branch {
                collect_uses(s, out);
            }
        }
        Stmt::While { cond, body } => {
            collect_expr_uses(cond, out);
            for s in body {
                collect_uses(s, out);
            }
        }
        Stmt::For { init, cond, step, body } => {
            if let Some(init) = init {
                collect_uses(init, out);
            }
            if let Some(cond) = cond {
                collect_expr_uses(cond, out);
            }
            if let Some(step) = step {
                collect_uses(step, out);
            }
            for s in body {
                collect_uses(s, out);
            }
        }
        Stmt::ForEach { iter, body, .. } => {
            collect_expr_uses(iter, out);
            for s in body {
                collect_uses(s, out);
            }
        }
        Stmt::Return(e) => collect_expr_uses(e, out),
        Stmt::Block(stmts) => {
            for s in stmts {
                collect_uses(s, out);
            }
        }
        Stmt::Raise(e) => collect_expr_uses(e, out),
        _ => {}
    }
}

fn collect_expr_uses(expr: &Expr, out: &mut Vec<String>) {
    match expr {
        Expr::Ident(name) => out.push(name.clone()),
        Expr::Call { args, .. } => {
            for e in args {
                collect_expr_uses(e, out);
            }
        }
        Expr::MethodCall { object, args, .. } => {
            collect_expr_uses(object, out);
            for e in args {
                collect_expr_uses(e, out);
            }
        }
        Expr::Binary { left, right, .. } => {
            collect_expr_uses(left, out);
            collect_expr_uses(right, out);
        }
        Expr::Unary { operand, .. } => collect_expr_uses(operand, out),
        Expr::Field { object, .. } => collect_expr_uses(object, out),
        Expr::Index { object, index } => {
            collect_expr_uses(object, out);
            collect_expr_uses(index, out);
        }
        Expr::List(items) | Expr::Tuple(items) => {
            for e in items {
                collect_expr_uses(e, out);
            }
        }
        Expr::Map(entries) => {
            for (k, v) in entries {
                collect_expr_uses(k, out);
                collect_expr_uses(v, out);
            }
        }
        Expr::Lambda { body, .. } => {
            for s in body {
                collect_uses(s, out);
            }
        }
        Expr::Ternary { cond, then_expr, else_expr } => {
            collect_expr_uses(cond, out);
            collect_expr_uses(then_expr, out);
            collect_expr_uses(else_expr, out);
        }
        _ => {}
    }
}

/// Lint a WPF source for unknown element kinds. Returns (diagnostics, had_error).
pub fn lint_wpf(source: &str, file: Option<&str>) -> (Vec<Diagnostic>, bool) {
    let mut diagnostics = Vec::new();
    let mut had_error = false;
    for (idx, line) in source.lines().enumerate() {
        let trimmed = line.trim();
        if let Some(rest) = trimmed.strip_prefix("element.") {
            let kind: String = rest
                .chars()
                .take_while(|c| c.is_alphanumeric() || *c == '_')
                .collect();
            if !kind.is_empty() && !crate::wpf::is_element_kind(&kind) {
                diagnostics.push(
                    Diagnostic::error(format!("unknown element kind '{}'", kind))
                        .at(file, idx + 1, trimmed.find("element.").unwrap_or(0) + 1),
                );
                had_error = true;
            }
        }
    }
    (diagnostics, had_error)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_no_let_warning() {
        // `let` is invalid Wynxer syntax, so the parser also flags it; the
        // dedicated lint warning should still be present.
        let (diags, _err) = lint_script("let x = 5;\n", Some("x.wynx"));
        assert!(diags.iter().any(|d| d.message.contains("not `let`")));
    }

    #[test]
    fn test_unused_variable() {
        let (diags, _) = lint_script("fn f() { int x = 5; }\n", Some("x.wynx"));
        assert!(diags.iter().any(|d| d.message.contains("never read")));
    }

    #[test]
    fn test_used_variable_no_warning() {
        let (diags, _) = lint_script("fn f() -> int { int x = 5; return x; }\n", None);
        assert!(!diags.iter().any(|d| d.message.contains("never read")));
    }

    #[test]
    fn test_unknown_element() {
        let (diags, err) = lint_wpf("element.foo(\"x\");\n", Some("x.wpf"));
        assert!(err);
        assert!(diags.iter().any(|d| d.message.contains("unknown element kind")));
    }
}
