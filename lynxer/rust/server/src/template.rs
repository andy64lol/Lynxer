//! A small Jinja-compatible template engine.
//!
//! It is not Jinja2, but it covers the constructs a server template actually
//! uses: `{{ expression }}` output with filters, `{% if %}`/`{% elif %}`/
//! `{% else %}`, `{% for x in items %}` (with the `loop` variable), `{% set %}`
//! and `{# comments #}`. Expressions support literals, dotted paths, the
//! operators `or`/`and`/`not`/`==`/`!=`/`<`/`<=`/`>`/`>=`/`in`/`+`/`-`/`*`/
//! `/`/`%`/`~`, grouping and the `range()` function.
//!
//! Output expressions are HTML-escaped by default. Templates may opt out for a
//! trusted expression with the `safe` filter.

use std::collections::HashMap;

use serde_json::Value;

/// Renders `source` with `data` as the root context.
#[cfg(test)]
pub fn render(source: &str, data: &Value) -> Result<String, String> {
    render_with(source, data, |_| {
        Err("template loader is unavailable".to_string())
    })
}

pub fn render_with(
    source: &str,
    data: &Value,
    loader: impl Fn(&str) -> Result<String, String>,
) -> Result<String, String> {
    let mut context = Context::new(data.clone());
    let mut output = String::with_capacity(source.len());
    let nodes = parse(source);
    let mut blocks = HashMap::new();
    render_nodes(&nodes, &mut context, &mut output, &loader, &mut blocks, 0)?;
    Ok(output)
}

// --- lexer ------------------------------------------------------------------

enum Token {
    Text(String),
    Output(String),
    Block(String),
}

fn tokenize(source: &str) -> Vec<Token> {
    let bytes = source.as_bytes();
    let mut tokens = Vec::new();
    let mut index = 0;
    let mut text_start = 0;
    while index + 1 < bytes.len() {
        if bytes[index] != b'{' {
            index += 1;
            continue;
        }
        let closer = match bytes[index + 1] {
            b'{' => (true, "}}"),
            b'%' => (false, "%}"),
            b'#' => (true, "#}"),
            _ => {
                index += 1;
                continue;
            }
        };
        let (is_output, terminator) = closer;
        let Some(relative) = source[index + 2..].find(terminator) else {
            break;
        };
        let end = index + 2 + relative;
        let inner = &source[index + 2..end];
        if let Some(body) = source.get(text_start..index) {
            if !body.is_empty() {
                tokens.push(Token::Text(body.to_string()));
            }
        }
        if terminator != "#}" {
            if is_output {
                tokens.push(Token::Output(inner.trim().to_string()));
            } else {
                tokens.push(Token::Block(inner.trim().to_string()));
            }
        }
        index = end + terminator.len();
        text_start = index;
    }
    if let Some(body) = source.get(text_start..) {
        if !body.is_empty() {
            tokens.push(Token::Text(body.to_string()));
        }
    }
    tokens
}

// --- AST --------------------------------------------------------------------

#[derive(Clone)]
enum Node {
    Text(String),
    Output(Expr, bool),
    If(Vec<(Option<Expr>, Vec<Node>)>),
    For {
        variable: String,
        iterable: Expr,
        body: Vec<Node>,
    },
    Set {
        name: String,
        value: Expr,
    },
    Include(String),
    Extends(String),
    Block {
        name: String,
        body: Vec<Node>,
    },
    Macro {
        name: String,
        arguments: Vec<String>,
        body: Vec<Node>,
    },
}

#[derive(Clone)]
enum Expr {
    Literal(Value),
    Path(Vec<String>),
    Unary(UnOp, Box<Expr>),
    Binary(BinOp, Box<Expr>, Box<Expr>),
    Filter {
        base: Box<Expr>,
        name: String,
        arguments: Vec<Expr>,
    },
    Call {
        name: String,
        arguments: Vec<Expr>,
    },
}

#[derive(Clone, Copy)]
enum UnOp {
    Not,
    Negate,
}

#[derive(Clone, Copy)]
enum BinOp {
    Or,
    And,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    In,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Concat,
}

// --- parser -----------------------------------------------------------------

fn parse(source: &str) -> Vec<Node> {
    let mut parser = Parser {
        tokens: tokenize(source),
        position: 0,
    };
    parser.parse_nodes(&[])
}

struct Parser {
    tokens: Vec<Token>,
    position: usize,
}

impl Parser {
    fn peek_block(&self) -> Option<&str> {
        match self.tokens.get(self.position) {
            Some(Token::Block(body)) => Some(body.as_str()),
            _ => None,
        }
    }

    fn keyword(&self) -> Option<(&str, &str)> {
        let body = self.peek_block()?;
        let body = body.trim();
        match body.find(char::is_whitespace) {
            Some(index) => Some((&body[..index], body[index..].trim())),
            None => Some((body, "")),
        }
    }

    fn parse_nodes(&mut self, stops: &[&str]) -> Vec<Node> {
        let mut nodes = Vec::new();
        while self.position < self.tokens.len() {
            match &self.tokens[self.position] {
                Token::Text(text) => {
                    nodes.push(Node::Text(text.clone()));
                    self.position += 1;
                }
                Token::Output(expression) => {
                    let mut expression = expression.clone();
                    let safe = if let Some(pipe) = expression.rfind('|') {
                        if expression[pipe + 1..].trim() == "safe" {
                            expression = expression[..pipe].trim_end().to_string();
                            true
                        } else {
                            false
                        }
                    } else {
                        false
                    };
                    nodes.push(Node::Output(parse_expression(&expression), safe));
                    self.position += 1;
                }
                Token::Block(_) => {
                    let (keyword, rest) = match self.keyword() {
                        Some(pair) => (pair.0.to_string(), pair.1.to_string()),
                        None => break,
                    };
                    if stops.contains(&keyword.as_str()) {
                        break;
                    }
                    self.position += 1;
                    match keyword.as_str() {
                        "if" => nodes.push(self.parse_if(&rest)),
                        "for" => nodes.push(self.parse_for(&rest)),
                        "set" => {
                            if let Some((name, value)) = rest.split_once('=') {
                                nodes.push(Node::Set {
                                    name: name.trim().to_string(),
                                    value: parse_expression(value.trim()),
                                });
                            }
                        }
                        "include" => {
                            if let Some(path) = quoted_literal(&rest) {
                                nodes.push(Node::Include(path));
                            }
                        }
                        "extends" => {
                            if let Some(path) = quoted_literal(&rest) {
                                nodes.push(Node::Extends(path));
                            }
                        }
                        "block" => {
                            let body = self.parse_nodes(&["endblock"]);
                            if matches!(self.keyword(), Some(("endblock", _))) {
                                self.position += 1;
                            }
                            nodes.push(Node::Block {
                                name: rest.trim().to_string(),
                                body,
                            });
                        }
                        "macro" => {
                            let (name, arguments) = parse_macro_header(&rest);
                            let body = self.parse_nodes(&["endmacro"]);
                            if matches!(self.keyword(), Some(("endmacro", _))) {
                                self.position += 1;
                            }
                            nodes.push(Node::Macro {
                                name,
                                arguments,
                                body,
                            });
                        }
                        _ => {}
                    }

                    fn quoted_literal(source: &str) -> Option<String> {
                        let source = source.trim();
                        let quote = source.chars().next()?;
                        if (quote != '"' && quote != '\'')
                            || !source.ends_with(quote)
                            || source.len() < 2
                        {
                            return None;
                        }
                        Some(source[1..source.len() - 1].to_string())
                    }

                    fn parse_macro_header(header: &str) -> (String, Vec<String>) {
                        let Some(open) = header.find('(') else {
                            return (header.trim().to_string(), Vec::new());
                        };
                        let name = header[..open].trim().to_string();
                        let arguments = header[open + 1..]
                            .trim_end_matches(')')
                            .split(',')
                            .map(str::trim)
                            .filter(|name| !name.is_empty())
                            .map(str::to_string)
                            .collect();
                        (name, arguments)
                    }
                }
            }
        }
        nodes
    }

    fn parse_if(&mut self, condition: &str) -> Node {
        let mut branches = Vec::new();
        let mut current = Some(parse_expression(condition));
        loop {
            let body = self.parse_nodes(&["elif", "else", "endif"]);
            branches.push((current.take(), body));
            match self.keyword() {
                Some(("elif", rest)) => {
                    let rest = rest.to_string();
                    self.position += 1;
                    current = Some(parse_expression(&rest));
                }
                Some(("else", _)) => {
                    self.position += 1;
                    let body = self.parse_nodes(&["endif"]);
                    branches.push((None, body));
                    if matches!(self.keyword(), Some(("endif", _))) {
                        self.position += 1;
                    }
                    break;
                }
                Some(("endif", _)) => {
                    self.position += 1;
                    break;
                }
                _ => break,
            }
        }
        Node::If(branches)
    }

    fn parse_for(&mut self, header: &str) -> Node {
        // "x in expression"
        let (variable, iterable) = match header.split_once(" in ") {
            Some((name, rest)) => (name.trim().to_string(), rest.trim()),
            None => (header.trim().to_string(), ""),
        };
        let body = self.parse_nodes(&["endfor"]);
        if matches!(self.keyword(), Some(("endfor", _))) {
            self.position += 1;
        }
        Node::For {
            variable,
            iterable: parse_expression(iterable),
            body,
        }
    }
}

// --- expression parser ------------------------------------------------------

fn parse_expression(source: &str) -> Expr {
    let mut parser = ExprParser {
        tokens: expression_tokens(source),
        position: 0,
    };
    parser.parse_or()
}

#[derive(Debug, PartialEq)]
enum EToken {
    Name(String),
    Number(f64),
    Text(String),
    Operator(String),
    Open,
    Close,
    Comma,
}

fn expression_tokens(source: &str) -> Vec<EToken> {
    let characters: Vec<char> = source.chars().collect();
    let mut tokens = Vec::new();
    let mut index = 0;
    while index < characters.len() {
        let character = characters[index];
        if character.is_whitespace() {
            index += 1;
            continue;
        }
        if character == '(' {
            tokens.push(EToken::Open);
            index += 1;
            continue;
        }
        if character == ')' {
            tokens.push(EToken::Close);
            index += 1;
            continue;
        }
        if character == ',' {
            tokens.push(EToken::Comma);
            index += 1;
            continue;
        }
        if character == '\'' || character == '"' {
            let quote = character;
            index += 1;
            let mut text = String::new();
            while index < characters.len() && characters[index] != quote {
                if characters[index] == '\\' && index + 1 < characters.len() {
                    index += 1;
                    text.push(match characters[index] {
                        'n' => '\n',
                        't' => '\t',
                        other => other,
                    });
                } else {
                    text.push(characters[index]);
                }
                index += 1;
            }
            index += 1;
            tokens.push(EToken::Text(text));
            continue;
        }
        if character.is_ascii_digit() {
            let start = index;
            while index < characters.len()
                && (characters[index].is_ascii_digit() || characters[index] == '.')
            {
                index += 1;
            }
            let number: String = characters[start..index].iter().collect();
            tokens.push(EToken::Number(number.parse().unwrap_or(0.0)));
            continue;
        }
        if character.is_alphabetic() || character == '_' {
            let start = index;
            while index < characters.len()
                && (characters[index].is_alphanumeric() || characters[index] == '_')
            {
                index += 1;
            }
            let name: String = characters[start..index].iter().collect();
            tokens.push(EToken::Name(name));
            continue;
        }
        // Operators, longest first.
        let rest: String = characters[index..].iter().collect();
        let mut matched = None;
        for operator in [
            "==", "!=", "<=", ">=", "//", "~", "|", "+", "-", "*", "/", "%", "<", ">", ".",
        ] {
            if rest.starts_with(operator) {
                matched = Some(operator);
                break;
            }
        }
        if let Some(operator) = matched {
            tokens.push(EToken::Operator(operator.to_string()));
            index += operator.len();
        } else {
            index += 1;
        }
    }
    tokens
}

struct ExprParser {
    tokens: Vec<EToken>,
    position: usize,
}

impl ExprParser {
    fn peek(&self) -> Option<&EToken> {
        self.tokens.get(self.position)
    }

    fn eat_operator(&mut self, operator: &str) -> bool {
        if let Some(EToken::Operator(found)) = self.peek() {
            if found == operator {
                self.position += 1;
                return true;
            }
        }
        false
    }

    fn eat_name(&mut self, name: &str) -> bool {
        if let Some(EToken::Name(found)) = self.peek() {
            if found == name {
                self.position += 1;
                return true;
            }
        }
        false
    }

    fn parse_or(&mut self) -> Expr {
        let mut left = self.parse_and();
        while self.eat_name("or") {
            let right = self.parse_and();
            left = Expr::Binary(BinOp::Or, Box::new(left), Box::new(right));
        }
        left
    }

    fn parse_and(&mut self) -> Expr {
        let mut left = self.parse_not();
        while self.eat_name("and") {
            let right = self.parse_not();
            left = Expr::Binary(BinOp::And, Box::new(left), Box::new(right));
        }
        left
    }

    fn parse_not(&mut self) -> Expr {
        if self.eat_name("not") {
            return Expr::Unary(UnOp::Not, Box::new(self.parse_not()));
        }
        self.parse_comparison()
    }

    fn parse_comparison(&mut self) -> Expr {
        let mut left = self.parse_additive();
        loop {
            let operator = match self.peek() {
                Some(EToken::Operator(found)) => match found.as_str() {
                    "==" => Some(BinOp::Equal),
                    "!=" => Some(BinOp::NotEqual),
                    "<" => Some(BinOp::Less),
                    "<=" => Some(BinOp::LessEqual),
                    ">" => Some(BinOp::Greater),
                    ">=" => Some(BinOp::GreaterEqual),
                    _ => None,
                },
                Some(EToken::Name(found)) if found == "in" => Some(BinOp::In),
                _ => None,
            };
            match operator {
                Some(operator) => {
                    self.position += 1;
                    let right = self.parse_additive();
                    left = Expr::Binary(operator, Box::new(left), Box::new(right));
                }
                None => return left,
            }
        }
    }

    fn parse_additive(&mut self) -> Expr {
        let mut left = self.parse_multiplicative();
        loop {
            let operator = if self.eat_operator("+") {
                Some(BinOp::Add)
            } else if self.eat_operator("-") {
                Some(BinOp::Subtract)
            } else if self.eat_operator("~") {
                Some(BinOp::Concat)
            } else {
                None
            };
            match operator {
                Some(operator) => {
                    let right = self.parse_multiplicative();
                    left = Expr::Binary(operator, Box::new(left), Box::new(right));
                }
                None => return left,
            }
        }
    }

    fn parse_multiplicative(&mut self) -> Expr {
        let mut left = self.parse_unary();
        loop {
            let operator = if self.eat_operator("*") {
                Some(BinOp::Multiply)
            } else if self.eat_operator("//") || self.eat_operator("/") {
                Some(BinOp::Divide)
            } else if self.eat_operator("%") {
                Some(BinOp::Modulo)
            } else {
                None
            };
            match operator {
                Some(operator) => {
                    let right = self.parse_unary();
                    left = Expr::Binary(operator, Box::new(left), Box::new(right));
                }
                None => return left,
            }
        }
    }

    fn parse_unary(&mut self) -> Expr {
        if self.eat_operator("-") {
            return Expr::Unary(UnOp::Negate, Box::new(self.parse_unary()));
        }
        self.parse_postfix()
    }

    fn parse_postfix(&mut self) -> Expr {
        let mut expression = self.parse_primary();
        while self.eat_operator("|") {
            let name = match self.peek() {
                Some(EToken::Name(name)) => {
                    let name = name.clone();
                    self.position += 1;
                    name
                }
                _ => break,
            };
            let mut arguments = Vec::new();
            if matches!(self.peek(), Some(EToken::Open)) {
                arguments = self.parse_arguments();
            }
            expression = Expr::Filter {
                base: Box::new(expression),
                name,
                arguments,
            };
        }
        expression
    }

    fn parse_arguments(&mut self) -> Vec<Expr> {
        let mut arguments = Vec::new();
        if !matches!(self.peek(), Some(EToken::Open)) {
            return arguments;
        }
        self.position += 1;
        loop {
            if matches!(self.peek(), Some(EToken::Close)) {
                self.position += 1;
                break;
            }
            arguments.push(self.parse_or());
            match self.peek() {
                Some(EToken::Comma) => {
                    self.position += 1;
                }
                _ => {
                    if matches!(self.peek(), Some(EToken::Close)) {
                        self.position += 1;
                    }
                    break;
                }
            }
        }
        arguments
    }

    fn parse_primary(&mut self) -> Expr {
        match self.peek() {
            Some(EToken::Number(number)) => {
                let number = *number;
                self.position += 1;
                Expr::Literal(serde_json::json!(number))
            }
            Some(EToken::Text(text)) => {
                let text = text.clone();
                self.position += 1;
                Expr::Literal(Value::String(text))
            }
            Some(EToken::Open) => {
                self.position += 1;
                let expression = self.parse_or();
                if matches!(self.peek(), Some(EToken::Close)) {
                    self.position += 1;
                }
                expression
            }
            Some(EToken::Name(name)) => {
                let name = name.clone();
                self.position += 1;
                match name.as_str() {
                    "true" | "True" => return Expr::Literal(Value::Bool(true)),
                    "false" | "False" => return Expr::Literal(Value::Bool(false)),
                    "none" | "None" | "null" => return Expr::Literal(Value::Null),
                    _ => {}
                }
                if matches!(self.peek(), Some(EToken::Open)) {
                    let arguments = self.parse_arguments();
                    return Expr::Call { name, arguments };
                }
                let mut path = vec![name];
                while self.eat_operator(".") {
                    if let Some(EToken::Name(part)) = self.peek() {
                        path.push(part.clone());
                        self.position += 1;
                    } else {
                        break;
                    }
                }
                Expr::Path(path)
            }
            _ => Expr::Literal(Value::Null),
        }
    }
}

// --- evaluation -------------------------------------------------------------

struct Context {
    scopes: Vec<HashMap<String, Value>>,
    loop_variables: Vec<HashMap<String, Value>>,
    macros: HashMap<String, (Vec<String>, Vec<Node>)>,
    error: Option<String>,
}

impl Context {
    fn new(root: Value) -> Self {
        let mut scope = HashMap::new();
        if let Value::Object(map) = root {
            for (key, value) in map {
                scope.insert(key, value);
            }
        }
        Context {
            scopes: vec![scope],
            loop_variables: vec![HashMap::new()],
            macros: HashMap::new(),
            error: None,
        }
    }

    fn lookup(&self, name: &str) -> Option<&Value> {
        for (index, scope) in self.scopes.iter().enumerate().rev() {
            if let Some(value) = scope.get(name) {
                return Some(value);
            }
            if let Some(value) = self
                .loop_variables
                .get(index)
                .and_then(|vars| vars.get(name))
            {
                return Some(value);
            }
        }
        None
    }

    fn set(&mut self, name: &str, value: Value) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.insert(name.to_string(), value);
        }
    }

    fn push_scope(&mut self) {
        self.scopes.push(HashMap::new());
        self.loop_variables.push(HashMap::new());
    }

    fn pop_scope(&mut self) {
        self.scopes.pop();
        self.loop_variables.pop();
    }

    fn set_loop(&mut self, name: &str, value: Value) {
        if let Some(vars) = self.loop_variables.last_mut() {
            vars.insert(name.to_string(), value);
        }
    }

    fn resolve(&mut self, path: &[String]) -> Value {
        let Some(mut value) = self.lookup(&path[0]).cloned() else {
            self.error = Some(format!("unknown template variable '{}'", path.join(".")));
            return Value::Null;
        };
        for part in &path[1..] {
            value = match &value {
                Value::Object(map) => match map.get(part) {
                    Some(value) => value.clone(),
                    None => {
                        self.error =
                            Some(format!("unknown template variable '{}'", path.join(".")));
                        return Value::Null;
                    }
                },
                Value::Array(items) => {
                    match part.parse::<usize>().ok().and_then(|i| items.get(i)) {
                        Some(value) => value.clone(),
                        None => {
                            self.error =
                                Some(format!("unknown template variable '{}'", path.join(".")));
                            return Value::Null;
                        }
                    }
                }
                _ => {
                    self.error = Some(format!("unknown template variable '{}'", path.join(".")));
                    return Value::Null;
                }
            };
        }
        value
    }
}

fn render_nodes(
    nodes: &[Node],
    context: &mut Context,
    output: &mut String,
    loader: &impl Fn(&str) -> Result<String, String>,
    overrides: &HashMap<String, Vec<Node>>,
    depth: usize,
) -> Result<(), String> {
    if depth > MAX_RENDER_DEPTH {
        return Err("template nesting limit exceeded".to_string());
    }
    collect_macros(nodes, context);
    if let Some(parent) = nodes.iter().find_map(|node| match node {
        Node::Extends(path) => Some(path.as_str()),
        _ => None,
    }) {
        let mut child_overrides = overrides.clone();
        for node in nodes {
            if let Node::Block { name, body } = node {
                child_overrides
                    .entry(name.clone())
                    .or_insert_with(|| body.clone());
            }
        }
        let parent_source = loader(parent)?;
        return render_nodes(
            &parse(&parent_source),
            context,
            output,
            loader,
            &child_overrides,
            depth + 1,
        );
    }
    execute_nodes(nodes, context, output, loader, overrides, depth)
}

const MAX_RENDER_DEPTH: usize = 64;

fn collect_macros(nodes: &[Node], context: &mut Context) {
    for node in nodes {
        match node {
            Node::Macro {
                name,
                arguments,
                body,
            } => {
                context
                    .macros
                    .entry(name.clone())
                    .or_insert_with(|| (arguments.clone(), body.clone()));
            }
            Node::If(branches) => {
                for (_, body) in branches {
                    collect_macros(body, context);
                }
            }
            Node::For { body, .. } | Node::Block { body, .. } => collect_macros(body, context),
            _ => {}
        }
    }
}

fn execute_nodes(
    nodes: &[Node],
    context: &mut Context,
    output: &mut String,
    loader: &impl Fn(&str) -> Result<String, String>,
    overrides: &HashMap<String, Vec<Node>>,
    depth: usize,
) -> Result<(), String> {
    if depth > MAX_RENDER_DEPTH {
        return Err("template nesting limit exceeded".to_string());
    }
    for node in nodes {
        match node {
            Node::Text(text) => output.push_str(text),
            Node::Output(expression, safe) => {
                if let Expr::Call { name, arguments } = expression {
                    if let Some((parameters, body)) = context.macros.get(name).cloned() {
                        let values = arguments
                            .iter()
                            .map(|argument| evaluate(argument, context))
                            .collect::<Vec<_>>();
                        context.push_scope();
                        for (parameter, value) in parameters.iter().zip(values) {
                            context.set(parameter, value);
                        }
                        let mut rendered = String::new();
                        execute_nodes(&body, context, &mut rendered, loader, overrides, depth + 1)?;
                        context.pop_scope();
                        output.push_str(&rendered);
                        continue;
                    }
                }
                let rendered = stringify(&evaluate(expression, context));
                if *safe {
                    output.push_str(&rendered);
                } else {
                    output.push_str(&escape_output(&rendered, output));
                }
            }
            Node::If(branches) => {
                for (condition, body) in branches {
                    let taken = match condition {
                        Some(condition) => truthy(&evaluate(condition, context)),
                        None => true,
                    };
                    if taken {
                        context.push_scope();
                        execute_nodes(body, context, output, loader, overrides, depth + 1)?;
                        context.pop_scope();
                        break;
                    }
                }
            }
            Node::For {
                variable,
                iterable,
                body,
            } => {
                let items = iterable_items(&evaluate(iterable, context));
                let length = items.len();
                for (index, item) in items.into_iter().enumerate() {
                    context.push_scope();
                    context.set(variable, item);
                    let mut loop_object = HashMap::new();
                    loop_object.insert("index".to_string(), serde_json::json!(index + 1));
                    loop_object.insert("index0".to_string(), serde_json::json!(index));
                    loop_object.insert("first".to_string(), Value::Bool(index == 0));
                    loop_object.insert("last".to_string(), Value::Bool(index + 1 == length));
                    loop_object.insert("length".to_string(), serde_json::json!(length));
                    context.set_loop("loop", Value::Object(loop_object.into_iter().collect()));
                    execute_nodes(body, context, output, loader, overrides, depth + 1)?;
                    context.pop_scope();
                }
            }
            Node::Set { name, value } => {
                let value = evaluate(value, context);
                context.set(name, value);
            }
            Node::Include(path) => {
                let included = loader(path)?;
                render_nodes(
                    &parse(&included),
                    context,
                    output,
                    loader,
                    overrides,
                    depth + 1,
                )?;
            }
            Node::Block { name, body } => {
                let selected = overrides.get(name).unwrap_or(body);
                execute_nodes(selected, context, output, loader, overrides, depth + 1)?;
            }
            Node::Macro {
                name,
                arguments,
                body,
            } => {
                context
                    .macros
                    .insert(name.clone(), (arguments.clone(), body.clone()));
            }
            Node::Extends(_) => {}
        }
        if let Some(error) = context.error.take() {
            return Err(error);
        }
    }
    Ok(())
}

fn escape_html(text: &str) -> String {
    let mut escaped = String::with_capacity(text.len());
    for character in text.chars() {
        match character {
            '&' => escaped.push_str("&amp;"),
            '<' => escaped.push_str("&lt;"),
            '>' => escaped.push_str("&gt;"),
            '"' => escaped.push_str("&quot;"),
            '\'' => escaped.push_str("&#x27;"),
            other => escaped.push(other),
        }
    }
    escaped
}

fn escape_output(text: &str, output: &str) -> String {
    if let Some((attribute, prefix)) = current_attribute_context(output) {
        if attribute.starts_with("on")
            || matches!(attribute.as_str(), "style" | "srcdoc" | "srcset")
        {
            return String::new();
        }
        if matches!(
            attribute.as_str(),
            "href" | "src" | "action" | "formaction" | "xlink:href" | "poster" | "data"
        ) {
            let normalized = format!("{prefix}{text}")
                .chars()
                .filter(|character| !character.is_control() && !character.is_whitespace())
                .collect::<String>()
                .to_ascii_lowercase();
            if normalized.starts_with("javascript:")
                || normalized.starts_with("vbscript:")
                || normalized.starts_with("data:")
            {
                return "#".to_string();
            }
        }
    }
    escape_html(text)
}

fn current_attribute_context(output: &str) -> Option<(String, String)> {
    let start = output.rfind('<')?;
    let tag = &output[start..];
    if tag.contains('>') || tag.starts_with("</") || tag.starts_with("<!") {
        return None;
    }
    let bytes = tag.as_bytes();
    let mut index = 1;
    while index < bytes.len() && !bytes[index].is_ascii_whitespace() {
        index += 1;
    }
    while index < bytes.len() {
        while index < bytes.len() && bytes[index].is_ascii_whitespace() {
            index += 1;
        }
        if index >= bytes.len() || bytes[index] == b'/' {
            return None;
        }
        let name_start = index;
        while index < bytes.len()
            && !bytes[index].is_ascii_whitespace()
            && !matches!(bytes[index], b'=' | b'>' | b'/')
        {
            index += 1;
        }
        if name_start == index {
            return None;
        }
        let name = tag[name_start..index].to_ascii_lowercase();
        while index < bytes.len() && bytes[index].is_ascii_whitespace() {
            index += 1;
        }
        if index >= bytes.len() || bytes[index] != b'=' {
            continue;
        }
        index += 1;
        while index < bytes.len() && bytes[index].is_ascii_whitespace() {
            index += 1;
        }
        if index >= bytes.len() {
            return Some((name, String::new()));
        }
        let quote = if matches!(bytes[index], b'\'' | b'"') {
            let quote = bytes[index];
            index += 1;
            Some(quote)
        } else {
            None
        };
        let value_start = index;
        while index < bytes.len()
            && match quote {
                Some(quote) => bytes[index] != quote,
                None => !bytes[index].is_ascii_whitespace(),
            }
        {
            index += 1;
        }
        if index == bytes.len() {
            return Some((name, tag[value_start..].to_string()));
        }
        if quote.is_some() {
            index += 1;
        }
    }
    None
}

fn iterable_items(value: &Value) -> Vec<Value> {
    match value {
        Value::Array(items) => items.clone(),
        Value::Object(map) => map.keys().map(|key| Value::String(key.clone())).collect(),
        Value::String(text) => text.chars().map(|c| Value::String(c.to_string())).collect(),
        _ => Vec::new(),
    }
}

fn evaluate(expression: &Expr, context: &mut Context) -> Value {
    match expression {
        Expr::Literal(value) => value.clone(),
        Expr::Path(path) => context.resolve(path),
        Expr::Unary(operator, operand) => {
            let value = evaluate(operand, context);
            match operator {
                UnOp::Not => Value::Bool(!truthy(&value)),
                UnOp::Negate => match as_number(&value) {
                    Some(number) => serde_json::json!(-number),
                    None => Value::Null,
                },
            }
        }
        Expr::Binary(operator, left, right) => {
            // `and`/`or` short-circuit.
            match operator {
                BinOp::And => {
                    let left = evaluate(left, context);
                    if !truthy(&left) {
                        return Value::Bool(false);
                    }
                    return Value::Bool(truthy(&evaluate(right, context)));
                }
                BinOp::Or => {
                    let left = evaluate(left, context);
                    if truthy(&left) {
                        return Value::Bool(true);
                    }
                    return Value::Bool(truthy(&evaluate(right, context)));
                }
                _ => {}
            }
            let left = evaluate(left, context);
            let right = evaluate(right, context);
            apply_binary(*operator, &left, &right)
        }
        Expr::Filter {
            base,
            name,
            arguments,
        } => {
            let value = evaluate(base, context);
            if name == "default" {
                let missing = context.error.take().is_some();
                let arguments: Vec<Value> = arguments
                    .iter()
                    .map(|argument| evaluate(argument, context))
                    .collect();
                if missing
                    || value.is_null()
                    || matches!(&value, Value::String(text) if text.is_empty())
                {
                    return arguments.first().cloned().unwrap_or(Value::Null);
                }
                return value;
            }
            let arguments: Vec<Value> = arguments
                .iter()
                .map(|argument| evaluate(argument, context))
                .collect();
            apply_filter(name, &value, &arguments)
        }
        Expr::Call { name, arguments } => {
            let arguments: Vec<Value> = arguments
                .iter()
                .map(|argument| evaluate(argument, context))
                .collect();
            apply_call(name, &arguments)
        }
    }
}

fn as_number(value: &Value) -> Option<f64> {
    match value {
        Value::Number(number) => number.as_f64(),
        Value::Bool(flag) => Some(if *flag { 1.0 } else { 0.0 }),
        Value::String(text) => text.trim().parse().ok(),
        _ => None,
    }
}

fn apply_binary(operator: BinOp, left: &Value, right: &Value) -> Value {
    match operator {
        BinOp::Equal => Value::Bool(left == right),
        BinOp::NotEqual => Value::Bool(left != right),
        BinOp::Less | BinOp::LessEqual | BinOp::Greater | BinOp::GreaterEqual => {
            let result = match (as_number(left), as_number(right)) {
                (Some(a), Some(b)) => a
                    .partial_cmp(&b)
                    .map(|ordering| compare_ordering(ordering, operator))
                    .unwrap_or(false),
                _ => match (left, right) {
                    (Value::String(a), Value::String(b)) => compare_ordering(a.cmp(b), operator),
                    _ => false,
                },
            };
            Value::Bool(result)
        }
        BinOp::In => Value::Bool(contains(right, left)),
        BinOp::Add | BinOp::Subtract | BinOp::Multiply | BinOp::Divide | BinOp::Modulo => {
            let (Some(a), Some(b)) = (as_number(left), as_number(right)) else {
                return Value::Null;
            };
            let result = match operator {
                BinOp::Add => a + b,
                BinOp::Subtract => a - b,
                BinOp::Multiply => a * b,
                BinOp::Divide => {
                    if b == 0.0 {
                        return Value::Null;
                    }
                    a / b
                }
                _ => {
                    if b == 0.0 {
                        return Value::Null;
                    }
                    a % b
                }
            };
            serde_json::json!(result)
        }
        BinOp::Concat => Value::String(format!("{}{}", stringify(left), stringify(right))),
        _ => Value::Null,
    }
}

fn compare_ordering(ordering: std::cmp::Ordering, operator: BinOp) -> bool {
    match operator {
        BinOp::Less => ordering == std::cmp::Ordering::Less,
        BinOp::LessEqual => ordering != std::cmp::Ordering::Greater,
        BinOp::Greater => ordering == std::cmp::Ordering::Greater,
        _ => ordering != std::cmp::Ordering::Less,
    }
}

fn contains(haystack: &Value, needle: &Value) -> bool {
    match haystack {
        Value::Array(items) => items.contains(needle),
        Value::Object(map) => match needle {
            Value::String(key) => map.contains_key(key),
            _ => false,
        },
        Value::String(text) => match needle {
            Value::String(part) => text.contains(part),
            _ => false,
        },
        _ => false,
    }
}

fn apply_filter(name: &str, value: &Value, arguments: &[Value]) -> Value {
    match name {
        "upper" => Value::String(stringify(value).to_uppercase()),
        "lower" => Value::String(stringify(value).to_lowercase()),
        "trim" => Value::String(stringify(value).trim().to_string()),
        "capitalize" => {
            let text = stringify(value);
            let mut characters = text.chars();
            match characters.next() {
                Some(first) => Value::String(
                    first.to_uppercase().collect::<String>() + &characters.as_str().to_lowercase(),
                ),
                None => Value::String(String::new()),
            }
        }
        "title" => Value::String(
            stringify(value)
                .split_whitespace()
                .map(|word| {
                    let mut characters = word.chars();
                    match characters.next() {
                        Some(first) => {
                            first.to_uppercase().collect::<String>()
                                + &characters.as_str().to_lowercase()
                        }
                        None => String::new(),
                    }
                })
                .collect::<Vec<_>>()
                .join(" "),
        ),
        "length" | "count" => Value::Number(
            match value {
                Value::Array(items) => serde_json::Number::from(items.len()),
                Value::Object(map) => serde_json::Number::from(map.len()),
                Value::String(text) => serde_json::Number::from(text.chars().count()),
                _ => serde_json::Number::from(0),
            }
            .into(),
        ),
        "first" => match value {
            Value::Array(items) => items.first().cloned().unwrap_or(Value::Null),
            Value::String(text) => Value::String(
                text.chars()
                    .next()
                    .map(|c| c.to_string())
                    .unwrap_or_default(),
            ),
            _ => Value::Null,
        },
        "last" => match value {
            Value::Array(items) => items.last().cloned().unwrap_or(Value::Null),
            Value::String(text) => Value::String(
                text.chars()
                    .last()
                    .map(|c| c.to_string())
                    .unwrap_or_default(),
            ),
            _ => Value::Null,
        },
        "reverse" => match value {
            Value::Array(items) => Value::Array(items.iter().rev().cloned().collect()),
            _ => Value::String(stringify(value).chars().rev().collect()),
        },
        "join" => {
            let separator = arguments.first().map(stringify).unwrap_or_default();
            let items = match value {
                Value::Array(items) => items.iter().map(stringify).collect::<Vec<_>>(),
                _ => vec![stringify(value)],
            };
            Value::String(items.join(&separator))
        }
        "default" => {
            if value.is_null() || matches!(value, Value::String(text) if text.is_empty()) {
                arguments.first().cloned().unwrap_or(Value::Null)
            } else {
                value.clone()
            }
        }
        "int" => {
            Value::Number(serde_json::Number::from(as_number(value).unwrap_or(0.0) as i64).into())
        }
        "float" => match serde_json::Number::from_f64(as_number(value).unwrap_or(0.0)) {
            Some(number) => Value::Number(number),
            None => Value::Null,
        },
        "round" => {
            let digits = arguments
                .first()
                .and_then(as_number)
                .unwrap_or(0.0)
                .max(0.0) as i32;
            let factor = 10f64.powi(digits);
            let rounded = (as_number(value).unwrap_or(0.0) * factor).round() / factor;
            match serde_json::Number::from_f64(rounded) {
                Some(number) => Value::Number(number),
                None => Value::Null,
            }
        }
        "replace" => {
            let from = arguments.first().map(stringify).unwrap_or_default();
            let to = arguments.get(1).map(stringify).unwrap_or_default();
            Value::String(stringify(value).replace(&from, &to))
        }
        "string" => Value::String(stringify(value)),
        "safe" => value.clone(),
        _ => value.clone(),
    }
}

fn apply_call(name: &str, arguments: &[Value]) -> Value {
    match name {
        "range" => {
            let (start, end, step) = match arguments.len() {
                1 => (0.0, as_number(&arguments[0]).unwrap_or(0.0), 1.0),
                2 => (
                    as_number(&arguments[0]).unwrap_or(0.0),
                    as_number(&arguments[1]).unwrap_or(0.0),
                    1.0,
                ),
                _ => (
                    as_number(&arguments[0]).unwrap_or(0.0),
                    as_number(&arguments[1]).unwrap_or(0.0),
                    as_number(&arguments[2]).unwrap_or(1.0),
                ),
            };
            let step = if step == 0.0 { 1.0 } else { step };
            let mut items = Vec::new();
            let mut value = start;
            let mut guard = 0;
            while (step > 0.0 && value < end) || (step < 0.0 && value > end) {
                items.push(serde_json::json!(value as i64));
                value += step;
                guard += 1;
                if guard > 1_000_000 {
                    break;
                }
            }
            Value::Array(items)
        }
        "length" | "count" => {
            apply_filter("length", arguments.first().unwrap_or(&Value::Null), &[])
        }
        _ => Value::Null,
    }
}

fn truthy(value: &Value) -> bool {
    match value {
        Value::Null => false,
        Value::Bool(flag) => *flag,
        Value::Number(number) => number.as_f64().map(|value| value != 0.0).unwrap_or(false),
        Value::String(text) => !text.is_empty(),
        Value::Array(items) => !items.is_empty(),
        Value::Object(map) => !map.is_empty(),
    }
}

fn stringify(value: &Value) -> String {
    match value {
        Value::Null => String::new(),
        Value::String(text) => text.clone(),
        Value::Bool(flag) => flag.to_string(),
        other => other.to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::{render, render_with};
    use serde_json::json;

    #[test]
    fn substitution_and_paths() {
        let data = json!({"name": "Ada", "user": {"city": "London"}});
        assert_eq!(render("Hi {{ name }}!", &data).unwrap(), "Hi Ada!");
        assert_eq!(render("{{ user.city }}", &data).unwrap(), "London");
        assert_eq!(
            render("{{ missing }}", &data).unwrap_err(),
            "unknown template variable 'missing'"
        );
    }

    #[test]
    fn conditionals() {
        let data = json!({"count": 3});
        assert_eq!(
            render("{% if count > 2 %}many{% else %}few{% endif %}", &data).unwrap(),
            "many"
        );
        assert_eq!(
            render(
                "{% if count > 5 %}many{% elif count > 1 %}some{% endif %}",
                &data
            )
            .unwrap(),
            "some"
        );
    }

    #[test]
    fn loops_with_loop_variable() {
        let data = json!({"items": ["a", "b"]});
        assert_eq!(
            render(
                "{% for i in items %}{{ loop.index }}:{{ i }} {% endfor %}",
                &data
            )
            .unwrap(),
            "1:a 2:b "
        );
        assert_eq!(
            render("{% for i in range(3) %}{{ i }}{% endfor %}", &data).unwrap(),
            "012"
        );
    }

    #[test]
    fn filters_and_set() {
        let data = json!({"name": "ada", "nothing": null});
        assert_eq!(render("{{ name | upper }}", &data).unwrap(), "ADA");
        assert_eq!(
            render("{{ nothing | default(\"x\") }}", &data).unwrap(),
            "x"
        );
        assert_eq!(
            render("{% set y = name ~ \"!\" %}{{ y }}", &data).unwrap(),
            "ada!"
        );
        assert_eq!(
            render("{% if not nothing %}ok{% endif %}", &data).unwrap(),
            "ok"
        );
        assert_eq!(
            render("{{ absent | default(\"fallback\") }}", &data).unwrap(),
            "fallback"
        );
    }

    #[test]
    fn comments_are_dropped() {
        assert_eq!(render("a{# hidden #}b", &json!({})).unwrap(), "ab");
    }

    #[test]
    fn output_is_escaped_unless_marked_safe() {
        let data = json!({"value": "<script a=\"b\">&"});
        assert_eq!(
            render("{{ value }}", &data).unwrap(),
            "&lt;script a=&quot;b&quot;&gt;&amp;"
        );
        assert_eq!(
            render("{{ value | safe }}", &data).unwrap(),
            "<script a=\"b\">&"
        );
        let dangerous_url = json!({"url": "java\nscript:alert(1)"});
        assert_eq!(
            render("<a href=\"{{ url }}\">link</a>", &dangerous_url).unwrap(),
            "<a href=\"#\">link</a>"
        );
        assert_eq!(
            render(
                "<button onclick=\"{{ value }}\" style=\"{{ value }}\" srcset=\"{{ value }}\">x</button>",
                &json!({"value": "alert(1)"})
            )
            .unwrap(),
            "<button onclick=\"\" style=\"\" srcset=\"\">x</button>"
        );
    }

    #[test]
    fn includes_inheritance_and_macros_render() {
        let templates = [
            (
                "base",
                "<h1>{% block title %}Base{% endblock %}</h1>{% block body %}{% endblock %}",
            ),
            ("part", "<b>{{ name }}</b>"),
        ];
        let loader = |name: &str| {
            templates
                .iter()
                .find(|(path, _)| *path == name)
                .map(|(_, text)| text.to_string())
                .ok_or_else(|| format!("missing test template {name}"))
        };
        let source = concat!(
            "{% extends \"base\" %}",
            "{% macro tag(value) %}<i>{{ value }}</i>{% endmacro %}",
            "{% block title %}{{ tag(name) }}{% endblock %}",
            "{% block body %}{% include \"part\" %}{% endblock %}"
        );
        assert_eq!(
            render_with(source, &json!({"name": "<Ada>"}), loader).unwrap(),
            "<h1><i>&lt;Ada&gt;</i></h1><b>&lt;Ada&gt;</b>"
        );
    }
}
