//! # Wynxer Page Format (`.wpf`)
//!
//! A very simplified DSL that describes a page's structure. Wynxer turns it
//! into HTML and CSS without the author ever writing HTML directly.
//!
//! ## Example
//!
//! ```text
//! import script app;
//! import styles styles;
//!
//! page index {
//!     page.title = "Main";
//!     contents {
//!         element.header1("Hello World!"){styles.blue-text};
//!         ignore(".blue-text behind the elements from the css");
//!         element.button("Click me!", app.onClick);
//!         element.newline();
//!         element.text("This is written in Wynxer!"){styles.blue-text};
//!         element.button("Greet me!", local.main);
//!     };
//!     scripts {
//!         // local scripts
//!         fn main() {
//!             alert("Hello!");
//!         }
//!         export main;
//!     }
//! }
//! ```

/// A WPF source file: a list of imports followed by one or more pages.
#[derive(Debug, Clone, Default)]
pub struct WpfFile {
    pub imports: Vec<Import>,
    pub pages: Vec<Page>,
}

#[derive(Debug, Clone)]
pub struct Import {
    pub kind: ImportKind,
    pub name: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImportKind {
    Script,
    Styles,
}

#[derive(Debug, Clone)]
pub struct Page {
    pub name: String,
    pub title: Option<String>,
    pub contents: Vec<Content>,
    pub scripts: Vec<Script>,
}

#[derive(Debug, Clone)]
pub enum Content {
    Element(Element),
    Ignore(String),
    Assignment { key: String, value: String },
}

#[derive(Debug, Clone)]
pub struct Element {
    pub kind: String,
    pub args: Vec<String>,
    pub style: Option<String>,
}

#[derive(Debug, Clone)]
pub struct Script {
    pub source: String,
    pub exports: Vec<String>,
}

/// Parse a `.wpf` source file.
pub fn parse(source: &str) -> Result<WpfFile, String> {
    let mut file = WpfFile::default();
    let lines: Vec<&str> = source.lines().collect();
    let mut i = 0;
    // Imports first.
    while i < lines.len() && lines[i].trim_start().starts_with("import ") {
        let line = lines[i].trim();
        let rest = line.trim_start_matches("import ").trim();
        // "script app;" or "styles styles;"
        let parts: Vec<&str> = rest.split_whitespace().collect();
        if parts.len() >= 2 {
            let kind = match parts[0] {
                "script" => ImportKind::Script,
                "styles" => ImportKind::Styles,
                _ => return Err(format!("unknown import kind '{}'", parts[0])),
            };
            let name = parts[1].trim_end_matches(';').to_string();
            file.imports.push(Import { kind, name });
        }
        i += 1;
    }
    // Pages.
    while i < lines.len() {
        let line = lines[i].trim();
        if line.is_empty() || line.starts_with("//") {
            i += 1;
            continue;
        }
        if line.starts_with("page ") {
            let name = line.trim_start_matches("page ").trim_end_matches('{').trim().to_string();
            // Collect the body until the matching closing brace.
            let mut depth;
            let mut body_lines: Vec<&str> = Vec::new();
            let mut j = i;
            // find opening brace
            while j < lines.len() && !lines[j].contains('{') {
                j += 1;
            }
            if j >= lines.len() {
                return Err(format!("page '{}' has no body", name));
            }
            depth = 1;
            j += 1;
            while j < lines.len() && depth > 0 {
                let l = lines[j];
                for c in l.chars() {
                    if c == '{' { depth += 1; }
                    else if c == '}' { depth -= 1; }
                }
                if depth > 0 {
                    body_lines.push(l);
                }
                j += 1;
            }
            let page = parse_page_body(&name, &body_lines)?;
            file.pages.push(page);
            i = j;
            continue;
        }
        i += 1;
    }
    Ok(file)
}

fn parse_page_body(name: &str, body: &[&str]) -> Result<Page, String> {
    let mut page = Page { name: name.to_string(), title: None, contents: Vec::new(), scripts: Vec::new() };
    let mut i = 0;
    while i < body.len() {
        let line = body[i].trim();
        if line.is_empty() || line.starts_with("//") {
            i += 1;
            continue;
        }
        if line.starts_with("page.title") {
            // page.title = "Main";
            if let Some(eq) = line.find('=') {
                let val = line[eq + 1..].trim().trim_end_matches(';').trim();
                page.title = Some(val.trim_matches('"').to_string());
            }
            i += 1;
            continue;
        }
        if line.starts_with("contents") {
            // contents { ... }
            let mut depth;
            let mut inner: Vec<&str> = Vec::new();
            let mut j = i;
            while j < body.len() && !body[j].contains('{') {
                j += 1;
            }
            if j >= body.len() {
                return Err("contents block has no body".into());
            }
            depth = 1;
            j += 1;
            while j < body.len() && depth > 0 {
                let l = body[j];
                for c in l.chars() {
                    if c == '{' { depth += 1; }
                    else if c == '}' { depth -= 1; }
                }
                if depth > 0 {
                    inner.push(l);
                }
                j += 1;
            }
            page.contents = parse_contents(&inner)?;
            i = j;
            continue;
        }
        if line.starts_with("scripts") {
            let mut depth;
            let mut inner: Vec<&str> = Vec::new();
            let mut j = i;
            while j < body.len() && !body[j].contains('{') {
                j += 1;
            }
            if j >= body.len() {
                return Err("scripts block has no body".into());
            }
            depth = 1;
            j += 1;
            while j < body.len() && depth > 0 {
                let l = body[j];
                for c in l.chars() {
                    if c == '{' { depth += 1; }
                    else if c == '}' { depth -= 1; }
                }
                if depth > 0 {
                    inner.push(l);
                }
                j += 1;
            }
            page.scripts.push(parse_scripts(&inner));
            i = j;
            continue;
        }
        i += 1;
    }
    Ok(page)
}

fn parse_contents(lines: &[&str]) -> Result<Vec<Content>, String> {
    let mut out = Vec::new();
    for line in lines {
        let line = line.trim();
        if line.is_empty() || line.starts_with("//") {
            continue;
        }
        if line.starts_with("ignore(") {
            // ignore("...");
            let start = line.find('(').unwrap_or(0);
            let end = line.rfind(')').unwrap_or(line.len());
            let val = line[start + 1..end].trim().trim_matches('"').to_string();
            out.push(Content::Ignore(val));
            continue;
        }
        if line.starts_with("element.") {
            let elem = parse_element(line)?;
            out.push(Content::Element(elem));
            continue;
        }
        if line.contains('=') && !line.starts_with("page.") {
            // generic assignment
            let parts: Vec<&str> = line.splitn(2, '=').collect();
            if parts.len() == 2 {
                out.push(Content::Assignment {
                    key: parts[0].trim().to_string(),
                    value: parts[1].trim().trim_end_matches(';').trim().to_string(),
                });
            }
            continue;
        }
    }
    Ok(out)
}

fn parse_element(line: &str) -> Result<Element, String> {
    // element.header1("Hello World!"){styles.blue-text};
    let mut rest = line.trim().trim_start_matches("element.").trim();
    rest = rest.trim_end_matches(';').trim();
    // kind
    let paren = rest.find('(').ok_or("element missing '('")?;
    let kind = rest[..paren].trim().to_string();
    // args
    let close_paren = rest.rfind(')').ok_or("element missing ')'")?;
    let args_str = &rest[paren + 1..close_paren];
    let args = split_args(args_str);
    // style binding {styles.blue-text}
    let style = if let Some(brace) = rest[close_paren + 1..].find('{') {
        let s = &rest[close_paren + 1 + brace..];
        let end = s.find('}').unwrap_or(s.len());
        Some(s[1..end].trim().to_string())
    } else {
        None
    };
    Ok(Element { kind, args, style })
}

fn split_args(s: &str) -> Vec<String> {
    let mut args = Vec::new();
    let mut current = String::new();
    let mut in_str = false;
    let mut depth = 0i32;
    for c in s.chars() {
        if in_str {
            current.push(c);
            if c == '"' {
                in_str = false;
            }
            continue;
        }
        match c {
            '"' => { in_str = true; current.push(c); }
            '(' | '{' | '[' => { depth += 1; current.push(c); }
            ')' | '}' | ']' => { depth -= 1; current.push(c); }
            ',' if depth == 0 => {
                let a = current.trim().to_string();
                if !a.is_empty() {
                    args.push(a);
                }
                current.clear();
            }
            _ => current.push(c),
        }
    }
    let a = current.trim().to_string();
    if !a.is_empty() {
        args.push(a);
    }
    args
}

fn parse_scripts(lines: &[&str]) -> Script {
    let mut script = Script { source: String::new(), exports: Vec::new() };
    for line in lines {
        let trimmed = line.trim();
        if trimmed.starts_with("export ") {
            let name = trimmed.trim_start_matches("export ").trim_end_matches(';').trim().to_string();
            script.exports.push(name);
        }
    }
    // Keep the full script block (including function bodies) so the
    // transpiler can emit complete functions.
    script.source = lines.join("\n");
    script
}

/// The set of element kinds Wynxer understands. New kinds are added here so
/// the HTML/CSS generators and the documentation stay in sync.
pub fn element_kinds() -> &'static [&'static str] {
    &[
        // Headings.
        "header1", "header2", "header3", "header4", "header5", "header6",
        // Text.
        "text", "paragraph", "span", "label", "caption", "quote", "code",
        // Interactive.
        "button", "link", "input", "textarea", "checkbox", "radio", "select",
        "option", "submit",
        // Layout.
        "div", "section", "article", "header", "footer", "nav", "aside",
        "main", "column", "row", "grid", "stack", "container", "card", "panel",
        // Media.
        "image", "video", "audio", "icon", "avatar",
        // Lists and tables.
        "list", "orderedList", "listItem", "table", "thead", "tbody", "tr",
        "th", "td",
        // Misc.
        "newline", "spacer", "divider", "hr", "br",
    ]
}

/// True if `kind` is a known element kind.
pub fn is_element_kind(kind: &str) -> bool {
    element_kinds().contains(&kind)
}
