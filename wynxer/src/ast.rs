//! Wynxer language AST.
//!
//! Wynxer borrows its surface syntax from Lynxer (`fn`, typed variables
//! `int x = 5;`, never `let`, control flow, macros) but targets the web.
//! The AST is the shared contract between the parser, the type checker and
//! the code generators.

#[derive(Debug, Clone)]
pub enum Expr {
    Int(i64),
    Float(f64),
    Str(String),
    Bool(bool),
    None,
    Ident(String),
    Call { callee: String, args: Vec<Expr> },
    MethodCall { object: Box<Expr>, method: String, args: Vec<Expr> },
    Binary { op: String, left: Box<Expr>, right: Box<Expr> },
    Unary { op: String, operand: Box<Expr> },
    Field { object: Box<Expr>, field: String },
    Index { object: Box<Expr>, index: Box<Expr> },
    List(Vec<Expr>),
    Tuple(Vec<Expr>),
    Map(Vec<(Expr, Expr)>),
    Lambda { params: Vec<(String, String)>, body: Vec<Stmt> },
    Ternary { cond: Box<Expr>, then_expr: Box<Expr>, else_expr: Box<Expr> },
}

#[derive(Debug, Clone)]
pub enum Stmt {
    // Typed variable declaration: `int x = 5;` (no `let`).
    Var { ty: String, name: String, value: Expr },
    // Assignment: `x = 5;`
    Assign { name: String, value: Expr },
    Expr(Expr),
    If { cond: Expr, then_branch: Vec<Stmt>, else_branch: Vec<Stmt> },
    While { cond: Expr, body: Vec<Stmt> },
    For { init: Option<Box<Stmt>>, cond: Option<Expr>, step: Option<Box<Stmt>>, body: Vec<Stmt> },
    ForEach { var: String, ty: Option<String>, iter: Expr, body: Vec<Stmt> },
    Return(Expr),
    Block(Vec<Stmt>),
    // Import a module: `import("dom");`
    Import(String),
    // Export a name: `export onClick;`
    Export(String),
    // Raise an exception: `raise expr;`
    Raise(Expr),
    // Try/catch.
    Try { body: Vec<Stmt>, catch_var: Option<String>, catch_body: Vec<Stmt> },
    // Switch/case.
    Switch { value: Expr, cases: Vec<(Expr, Vec<Stmt>)>, default: Option<Vec<Stmt>> },
    // Macro definition (placeholder).
    Macro { name: String, body: Vec<Stmt> },
}

#[derive(Debug, Clone)]
pub struct Function {
    pub name: String,
    pub pub_: bool,
    pub params: Vec<(String, String)>,  // (name, type)
    pub varargs: bool,                   // `*args` variadic
    pub return_type: Option<String>,
    pub body: Vec<Stmt>,
    pub is_async: bool,
    pub is_exported: bool,
}

#[derive(Debug, Clone)]
pub struct StructDef {
    pub name: String,
    pub fields: Vec<(String, String)>,  // (name, type)
}

#[derive(Debug, Clone)]
pub struct EnumDef {
    pub name: String,
    pub variants: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct MacroDef {
    pub pub_: bool,
    pub name: String,
    pub params: Vec<String>,      // parameter names (including `*args` marker)
    pub body: Vec<Stmt>,
}

#[derive(Debug, Clone)]
pub struct Module {
    pub name: Option<String>,
    pub functions: Vec<Function>,
    pub structs: Vec<StructDef>,
    pub enums: Vec<EnumDef>,
    pub imports: Vec<String>,
    pub exports: Vec<String>,
    pub macros: Vec<MacroDef>,
    /// Raw JavaScript statements injected via the JS ABI (`js("...");`).
    pub js_blocks: Vec<String>,
}
