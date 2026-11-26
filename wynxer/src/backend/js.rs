//! JavaScript code generator.
//!
//! Turns a parsed Wynxer `Module` into plain JavaScript. Functions are emitted
//! as top-level declarations so `onclick="onClick()"` handlers generated from
//! `.wpf` can call them. Wynxer's typed declarations (`int x = 5;`) become
//! `var x = 5;`, and control flow / expressions are translated structurally
//! rather than copied verbatim.

use crate::ast::{Expr, Module, Stmt};
use crate::jsabi::{self, Target};

/// Configure the emitter's module target (defaults to Browser).
pub fn emit_with_target(module: &Module, target: Target) -> String {
    let mut emitter = JsEmitter::new();
    emitter.target = target;
    emitter.emit_module(module)
}

/// Map a parser operator spelling (`EqEq`, `Plus`, ...) to its JS form.
fn op_to_js(op: &str) -> &str {
    match op {
        "EqEq" => "===",
        "BangEq" => "!==",
        "Lt" => "<",
        "Gt" => ">",
        "LtEq" => "<=",
        "GtEq" => ">=",
        "And" => "&&",
        "Or" => "||",
        "Plus" => "+",
        "Minus" => "-",
        "Star" => "*",
        "Slash" => "/",
        "Percent" => "%",
        "Assign" => "=",
        "Bang" => "!",
        other => other,
    }
}

pub struct JsEmitter {
    out: String,
    indent: usize,
    target: Target,
}

impl Default for JsEmitter {
    fn default() -> Self {
        Self::new()
    }
}

impl JsEmitter {
    pub fn new() -> Self {
        Self {
            out: String::new(),
            indent: 0,
            target: Target::Browser,
        }
    }

    fn line(&mut self, text: &str) {
        for _ in 0..self.indent {
            self.out.push_str("  ");
        }
        self.out.push_str(text);
        self.out.push('\n');
    }

    fn emit_expr(&mut self, expr: &Expr) -> String {
        match expr {
            Expr::Int(n) => n.to_string(),
            Expr::Float(f) => f.to_string(),
            Expr::Str(s) => format!("\"{}\"", escape_string(s)),
            Expr::Bool(b) => b.to_string(),
            Expr::None => "undefined".to_string(),
            Expr::Ident(name) => name.clone(),
            // ---- JS ABI builtins ----
            Expr::Call { callee, args } if jsabi::is_raw_expression(callee) => {
                jsabi::string_arg(args, 0).unwrap_or_else(|| "undefined".to_string())
            }
            Expr::Call { callee, args } if jsabi::is_require(callee) => {
                let pkg = jsabi::string_arg(args, 0).unwrap_or_default();
                format!("require(\"{}\")", pkg)
            }
            Expr::Call { callee, args } if jsabi::is_global(callee) => {
                jsabi::string_arg(args, 0).unwrap_or_else(|| "undefined".to_string())
            }
            Expr::Call { callee, args } if jsabi::is_import(callee) => {
                // Imports are emitted as statements by emit_module; as an
                // expression they resolve to the package's namespace.
                let pkg = jsabi::string_arg(args, 0).unwrap_or_default();
                match self.target {
                    Target::NodeCommonJs => format!("require(\"{}\")", pkg),
                    _ => format!("await import(\"{}\")", pkg),
                }
            }
            Expr::Call { callee, args } => {
                let args = args.iter().map(|e| self.emit_expr(e)).collect::<Vec<_>>();
                // Map Wynxer/Lynxer builtins to their web equivalents.
                let name = match callee.as_str() {
                    "println" | "print" => "console.log".to_string(),
                    "input" => "prompt".to_string(),
                    other => other.to_string(),
                };
                format!("{}({})", name, args.join(", "))
            }
            Expr::MethodCall {
                object,
                method,
                args,
            } => {
                let obj = self.emit_expr(object);
                let args = args.iter().map(|e| self.emit_expr(e)).collect::<Vec<_>>();
                format!("{}.{}({})", obj, method, args.join(", "))
            }
            Expr::Binary { op, left, right } => {
                let l = self.emit_expr(left);
                let r = self.emit_expr(right);
                format!("{} {} {}", l, op_to_js(op), r)
            }
            Expr::Unary { op, operand } => {
                let o = self.emit_expr(operand);
                format!("{}{}", op_to_js(op), o)
            }
            Expr::Field { object, field } => {
                let obj = self.emit_expr(object);
                format!("{}.{}", obj, field)
            }
            Expr::Index { object, index } => {
                let obj = self.emit_expr(object);
                let idx = self.emit_expr(index);
                format!("{}[{}]", obj, idx)
            }
            Expr::List(items) => {
                let items = items.iter().map(|e| self.emit_expr(e)).collect::<Vec<_>>();
                format!("[{}]", items.join(", "))
            }
            Expr::Tuple(items) => {
                let items = items.iter().map(|e| self.emit_expr(e)).collect::<Vec<_>>();
                format!("[{}]", items.join(", "))
            }
            Expr::Map(entries) => {
                let entries = entries
                    .iter()
                    .map(|(k, v)| format!("{}: {}", self.emit_expr(k), self.emit_expr(v)))
                    .collect::<Vec<_>>();
                format!("{{{}}}", entries.join(", "))
            }
            Expr::Lambda { params, body } => {
                let params = params
                    .iter()
                    .map(|(n, _)| n.clone())
                    .collect::<Vec<_>>()
                    .join(", ");
                let mut sub = JsEmitter::new();
                sub.indent = 0;
                for stmt in body {
                    sub.emit_stmt(stmt);
                }
                format!("function({}) {{\n{}}}", params, sub.out)
            }
            Expr::Ternary {
                cond,
                then_expr,
                else_expr,
            } => format!(
                "{} ? {} : {}",
                self.emit_expr(cond),
                self.emit_expr(then_expr),
                self.emit_expr(else_expr)
            ),
        }
    }

    fn emit_block(&mut self, stmts: &[Stmt]) {
        self.line("{");
        self.indent += 1;
        for stmt in stmts {
            self.emit_stmt(stmt);
        }
        self.indent -= 1;
        self.line("}");
    }

    fn emit_stmt(&mut self, stmt: &Stmt) {
        match stmt {
            Stmt::Var { name, value, .. } => {
                let v = self.emit_expr(value);
                self.line(&format!("var {} = {};", name, v));
            }
            Stmt::Assign { name, value } => {
                let v = self.emit_expr(value);
                self.line(&format!("{} = {};", name, v));
            }
            Stmt::Expr(expr) => {
                // Raw JS statement via the ABI: `js("...");`
                if let Expr::Call { callee, args } = expr {
                    if jsabi::is_raw_statement(callee) {
                        if let Some(code) = jsabi::string_arg(args, 0) {
                            // Raw code is emitted verbatim, one line at a time,
                            // with the current indentation preserved.
                            for raw in code.lines() {
                                self.line(raw);
                            }
                            return;
                        }
                    }
                }
                let e = self.emit_expr(expr);
                self.line(&format!("{};", e));
            }
            Stmt::If {
                cond,
                then_branch,
                else_branch,
            } => {
                let c = self.emit_expr(cond);
                self.line(&format!("if ({})", c));
                self.emit_block(then_branch);
                if !else_branch.is_empty() {
                    self.line("else");
                    self.emit_block(else_branch);
                }
            }
            Stmt::While { cond, body } => {
                let c = self.emit_expr(cond);
                self.line(&format!("while ({})", c));
                self.emit_block(body);
            }
            Stmt::For {
                init,
                cond,
                step,
                body,
            } => {
                let init = match init.as_deref() {
                    Some(Stmt::Var { name, value, .. }) => {
                        format!("var {} = {}", name, self.emit_expr(value))
                    }
                    Some(Stmt::Assign { name, value }) => {
                        format!("{} = {}", name, self.emit_expr(value))
                    }
                    Some(Stmt::Expr(e)) => self.emit_expr(e),
                    _ => String::new(),
                };
                let cond = cond
                    .as_ref()
                    .map(|e| self.emit_expr(e))
                    .unwrap_or_default();
                let step = match step.as_deref() {
                    Some(Stmt::Assign { name, value }) => {
                        format!("{} = {}", name, self.emit_expr(value))
                    }
                    Some(Stmt::Expr(e)) => self.emit_expr(e),
                    _ => String::new(),
                };
                self.line(&format!("for ({}; {}; {})", init, cond, step));
                self.emit_block(body);
            }
            Stmt::ForEach {
                var, iter, body, ..
            } => {
                let it = self.emit_expr(iter);
                self.line(&format!("for (var {} of {})", var, it));
                self.emit_block(body);
            }
            Stmt::Return(expr) => {
                if matches!(expr, Expr::None) {
                    self.line("return;");
                } else {
                    let e = self.emit_expr(expr);
                    self.line(&format!("return {};", e));
                }
            }
            Stmt::Block(stmts) => self.emit_block(stmts),
            Stmt::Import(name) => {
                self.line(&format!("// import {}", name));
            }
            Stmt::Export(name) => {
                self.line(&format!("// export {}", name));
            }
            Stmt::Raise(expr) => {
                let e = self.emit_expr(expr);
                self.line(&format!("throw {};", e));
            }
            Stmt::Try {
                body,
                catch_var,
                catch_body,
            } => {
                self.line("try");
                self.emit_block(body);
                let var = catch_var.clone().unwrap_or_else(|| "e".to_string());
                self.line(&format!("catch ({})", var));
                self.emit_block(catch_body);
            }
            Stmt::Switch {
                value,
                cases,
                default,
            } => {
                let v = self.emit_expr(value);
                self.line(&format!("switch ({})", v));
                self.line("{");
                // Cases are not Stmts, so emit the body manually.
                self.indent += 1;
                for (pat, body) in cases {
                    let p = self.emit_expr(pat);
                    self.line(&format!("case {}:", p));
                    self.indent += 1;
                    for s in body {
                        self.emit_stmt(s);
                    }
                    self.indent -= 1;
                }
                if let Some(body) = default {
                    self.line("default:");
                    self.indent += 1;
                    for s in body {
                        self.emit_stmt(s);
                    }
                    self.indent -= 1;
                }
                self.indent -= 1;
                self.line("}");
            }
            Stmt::Macro { name, body } => {
                self.line(&format!("// macro {}", name));
                for s in body {
                    self.emit_stmt(s);
                }
            }
        }
    }

    pub fn emit_module(&mut self, module: &Module) -> String {
        // ABI header.
        self.line(jsabi::header(self.target).trim_end());

        // Wynxer module imports that are not JS/npm packages stay as comments.
        for name in &module.imports {
            self.line(&format!("// import {}", name));
        }
        for name in &module.exports {
            self.line(&format!("// export {}", name));
        }

        // Hoist `jsImport("pkg", "names")` calls found in function bodies into
        // real module-level imports so ESM/CJS output is valid.
        let mut imports: Vec<String> = Vec::new();
        for func in &module.functions {
            collect_js_imports(&func.body, self.target, &mut imports);
        }
        for stmt in &module.js_blocks {
            // Top-level raw JS is emitted verbatim.
            for raw in stmt.lines() {
                self.line(raw);
            }
        }
        for imp in imports {
            self.line(&imp);
        }
        self.out.push('\n');

        for func in &module.functions {
            let params = func
                .params
                .iter()
                .map(|(n, _)| n.clone())
                .collect::<Vec<_>>();
            if func.varargs {
                // Collect extra args into an array named `args`.
                let mut ps = params.clone();
                ps.push("...args".to_string());
                self.line(&format!(
                    "function {}({})",
                    func.name,
                    ps.join(", ")
                ));
            } else {
                self.line(&format!(
                    "function {}({})",
                    func.name,
                    params.join(", ")
                ));
            }
            self.emit_block(&func.body);
            self.out.push('\n');
        }
        self.out.clone()
    }
}

/// Emit JavaScript for a parsed module.
pub fn emit(module: &Module) -> String {
    JsEmitter::new().emit_module(module)
}

fn escape_string(s: &str) -> String {
    s.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n")
}

/// Walk statements and collect `jsImport(...)` calls to hoist as module-level
/// imports. Avoids duplicates so a package imported in two functions is
/// emitted once.
fn collect_js_imports(stmts: &[Stmt], target: Target, out: &mut Vec<String>) {
    for stmt in stmts {
        match stmt {
            Stmt::Expr(Expr::Call { callee, args }) if jsabi::is_import(callee) => {
                let pkg = jsabi::string_arg(args, 0).unwrap_or_default();
                let names = jsabi::string_arg(args, 1).unwrap_or_default();
                if pkg.is_empty() {
                    continue;
                }
                let line = jsabi::emit_import(&pkg, &names, target);
                if !out.contains(&line) {
                    out.push(line);
                }
            }
            Stmt::If { then_branch, else_branch, .. } => {
                collect_js_imports(then_branch, target, out);
                collect_js_imports(else_branch, target, out);
            }
            Stmt::While { body, .. } | Stmt::ForEach { body, .. } => {
                collect_js_imports(body, target, out);
            }
            Stmt::For { body, .. } => collect_js_imports(body, target, out),
            Stmt::Block(body) => collect_js_imports(body, target, out),
            Stmt::Try { body, catch_body, .. } => {
                collect_js_imports(body, target, out);
                collect_js_imports(catch_body, target, out);
            }
            Stmt::Switch { cases, default, .. } => {
                for (_, body) in cases {
                    collect_js_imports(body, target, out);
                }
                if let Some(body) = default {
                    collect_js_imports(body, target, out);
                }
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_module;

    #[test]
    fn test_typed_decl_becomes_var() {
        let js = emit(&parse_module("fn onClick() { int x = 5; }").unwrap());
        assert!(js.contains("function onClick()"));
        assert!(js.contains("var x = 5;"));
        assert!(!js.contains("int x"));
        // No duplicated brace after the signature.
        assert!(!js.contains("} {"));
    }

    #[test]
    fn test_handler_function_is_global() {
        let js = emit(&parse_module("fn onClick() { alert(\"clicked\"); }").unwrap());
        assert!(js.contains("function onClick()"));
        assert!(js.contains("alert(\"clicked\");"));
    }

    #[test]
    fn test_typed_params_and_return() {
        let js = emit(&parse_module("fn greet(str name) -> str { return name; }").unwrap());
        assert!(js.contains("function greet(name)"));
        assert!(js.contains("return name;"));
        assert!(!js.contains("str name"));
        assert!(!js.contains("->"));
    }

    #[test]
    fn test_println_maps_to_console_log() {
        let js = emit(&parse_module("fn main() { println(\"Hi\"); }").unwrap());
        assert!(js.contains("console.log(\"Hi\");"));
        assert!(!js.contains("println("));
    }

    #[test]
    fn test_control_flow_and_varargs() {
        let src = "fn f(*args) { int x = 0; while (x < 3) { x = x + 1; } return; }";
        let js = emit(&parse_module(src).unwrap());
        assert!(js.contains("function f(...args)"));
        assert!(js.contains("while (x < 3)"));
        assert!(js.contains("var x = 0;"));
        assert!(js.contains("return;"));
    }

    #[test]
    fn test_raw_js_abi_statement() {
        let module = parse_module("fn f() { js(\"console.log('raw');\"); }").unwrap();
        let js = emit(&module);
        assert!(js.contains("console.log('raw');"));
        assert!(js.contains(jsabi::ABI_VERSION));
    }

    #[test]
    fn test_raw_js_abi_expression() {
        let module = parse_module("fn f() { int x = jsExpr(\"Date.now()\"); }").unwrap();
        let js = emit(&module);
        assert!(js.contains("var x = Date.now();"));
    }

    #[test]
    fn test_js_import_hoisting() {
        let module = parse_module("fn f() { jsImport(\"lodash\", \"map\"); }").unwrap();
        let js = emit_with_target(&module, Target::NodeModule);
        assert!(js.contains("import { map } from \"lodash\";"));
        let cjs = emit_with_target(&module, Target::NodeCommonJs);
        assert!(cjs.contains("const { map } = require(\"lodash\");"));
    }

    #[test]
    fn test_top_level_js_block() {
        let module = parse_module("js(\"const GLOBAL_X = 1;\"); fn f() { }").unwrap();
        assert_eq!(module.js_blocks.len(), 1);
        let js = emit(&module);
        assert!(js.contains("const GLOBAL_X = 1;"));
    }
}
