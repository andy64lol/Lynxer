//! Build, serve and check commands for a Wynxer project.

use crate::wpf;
use std::fs;
use std::path::Path;

/// Find the entry page file in a project directory.
pub fn find_entry_page(dir: &Path) -> Option<std::path::PathBuf> {
    let candidates = ["index.wpf", "Index.wpf", "home.wpf", "Home.wpf"];
    for c in candidates {
        let p = dir.join(c);
        if p.is_file() {
            return Some(p);
        }
    }
    // Fall back to the first .wpf file.
    if let Ok(entries) = fs::read_dir(dir) {
        for e in entries.flatten() {
            let p = e.path();
            if p.extension().and_then(|s| s.to_str()) == Some("wpf") {
                return Some(p);
            }
        }
    }
    None
}

/// Find the Wynxer script file.
pub fn find_script(dir: &Path) -> Option<std::path::PathBuf> {
    let candidates = ["app.wynx", "main.wynx", "index.wynx"];
    for c in candidates {
        let p = dir.join(c);
        if p.is_file() {
            return Some(p);
        }
    }
    if let Ok(entries) = fs::read_dir(dir) {
        for e in entries.flatten() {
            let p = e.path();
            if p.extension().and_then(|s| s.to_str()) == Some("wynx") {
                return Some(p);
            }
        }
    }
    None
}

/// Find CSS files.
pub fn find_styles(dir: &Path) -> Vec<std::path::PathBuf> {
    let mut out = Vec::new();
    if let Ok(entries) = fs::read_dir(dir) {
        for e in entries.flatten() {
            let p = e.path();
            if p.extension().and_then(|s| s.to_str()) == Some("css") {
                out.push(p);
            }
        }
    }
    out
}

/// Map a WPF element kind to an HTML tag.
pub fn element_tag(kind: &str) -> &'static str {
    match kind {
        "header1" => "h1",
        "header2" => "h2",
        "header3" => "h3",
        "header4" => "h4",
        "header5" => "h5",
        "header6" => "h6",
        "header" => "header",
        "text" => "p",
        "paragraph" => "p",
        "span" => "span",
        "label" => "label",
        "caption" => "figcaption",
        "quote" => "blockquote",
        "code" => "code",
        "button" => "button",
        "link" => "a",
        "input" => "input",
        "textarea" => "textarea",
        "checkbox" => "input",
        "radio" => "input",
        "submit" => "input",
        "div" => "div",
        "section" => "section",
        "article" => "article",
        "footer" => "footer",
        "nav" => "nav",
        "aside" => "aside",
        "main" => "main",
        "column" => "div",
        "row" => "div",
        "grid" => "div",
        "stack" => "div",
        "container" => "div",
        "card" => "div",
        "panel" => "div",
        "image" => "img",
        "video" => "video",
        "audio" => "audio",
        "icon" => "span",
        "avatar" => "img",
        "list" => "ul",
        "orderedList" => "ol",
        "listItem" => "li",
        "table" => "table",
        "thead" => "thead",
        "tbody" => "tbody",
        "tr" => "tr",
        "th" => "th",
        "td" => "td",
        "newline" => "br",
        "spacer" => "div",
        "divider" | "hr" => "hr",
        "br" => "br",
        _ => "div",
    }
}

/// Render a WPF element to an HTML string.
pub fn render_element(elem: &wpf::Element) -> String {
    let tag = element_tag(&elem.kind);
    let mut html = String::new();
    // Self-closing / void elements.
    let void_elem = matches!(tag, "br" | "hr" | "input" | "img");
    // Attributes.
    let mut attrs = String::new();
    if elem.kind == "checkbox" {
        attrs.push_str(" type=\"checkbox\"");
    } else if elem.kind == "radio" {
        attrs.push_str(" type=\"radio\"");
    } else if elem.kind == "submit" {
        attrs.push_str(" type=\"submit\"");
    } else if elem.kind == "image" || elem.kind == "avatar" {
        attrs.push_str(" alt=\"\"");
    }
    // Event handler binding: second arg like app.onClick or local.main.
    let mut onclick = String::new();
    if elem.args.len() >= 2 {
        let handler = elem.args[1].trim();
        if handler.starts_with("app.") {
            onclick = format!(" onclick=\"{}()\"", handler.trim_start_matches("app."));
        } else if handler.starts_with("local.") {
            onclick = format!(" onclick=\"{}()\"", handler.trim_start_matches("local."));
        }
    }
    if !onclick.is_empty() {
        attrs.push_str(&onclick);
    }
    // Style binding -> class.
    if let Some(style) = &elem.style {
        let class_name = style.rsplit('.').next().unwrap_or(style).to_string();
        attrs.push_str(&format!(" class=\"{}\"", class_name));
    }
    // Text content (first arg, if it looks like a string literal).
    let text = elem.args.first().map(|a| a.trim()).unwrap_or("");
    let text = text.trim_matches('"');

    if void_elem {
        html.push_str(&format!("<{}{} />", tag, attrs));
    } else if tag == "input" {
        html.push_str(&format!("<input{} value=\"{}\" />", attrs, text));
    } else if tag == "img" {
        html.push_str(&format!("<img src=\"\" alt=\"{}\"{} />", text, attrs));
    } else {
        html.push_str(&format!("<{}{}>{}</{}>", tag, attrs, text, tag));
    }
    html
}

/// Render a WPF page to a complete HTML document.
pub fn render_page(page: &wpf::Page, css_href: &str, js_href: &str) -> String {
    let title = page.title.clone().unwrap_or_else(|| page.name.clone());
    let mut body = String::new();
    for content in &page.contents {
        match content {
            wpf::Content::Element(e) => {
                body.push_str("    ");
                body.push_str(&render_element(e));
                body.push('\n');
            }
            wpf::Content::Ignore(_) => {}
            wpf::Content::Assignment { .. } => {}
        }
    }
    format!(
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"utf-8\" />\n  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n  <title>{}</title>\n  <link rel=\"stylesheet\" href=\"{}\" />\n</head>\n<body>\n{}\n  <script src=\"{}\"></script>\n</body>\n</html>\n",
        title, css_href, body, js_href
    )
}

/// Transpile a Wynxer script (.wynx) to JavaScript (best-effort).
/// Transpile a Wynxer script to JavaScript using the real parser/AST.
/// Falls back to the legacy line-based transpiler if parsing fails, so an
/// unfinished script still produces output rather than nothing.
pub fn transpile_script(source: &str) -> String {
    match crate::parser::parse_module(source) {
        Ok(module) => crate::backend::js::emit(&module),
        Err(_) => transpile_script_line_based(source),
    }
}

/// Legacy line-based transpiler (kept as a fallback for unparseable scripts).
fn transpile_script_line_based(source: &str) -> String {
    let mut out = String::new();
    for line in source.lines() {
        let t = line.trim();
        if t.is_empty() || t.starts_with("//") {
            continue;
        }
        if t.starts_with("fn ") {
            // fn name(params) {  ->  function name(params) {
            let sig = t.trim_start_matches("fn ").trim_end_matches('{').trim();
            out.push_str(&format!("function {} {{\n", sig));
            continue;
        }
        if t == "}" {
            out.push_str("}\n");
            continue;
        }
        if t.starts_with("export ") {
            let name = t.trim_start_matches("export ").trim_end_matches(';').trim();
            // Plain <script src="app.js"> is not an ES module, so a real
            // `export` statement would be a syntax error in the browser.
            out.push_str(&format!("// export {}\n", name));
            continue;
        }
        // alert("...") stays as-is; println -> console.log.
        let mut line_out = t.to_string();
        if line_out.starts_with("println(") {
            line_out = line_out.replacen("println(", "console.log(", 1);
        }
        if !line_out.ends_with(';') && !line_out.ends_with('{') && !line_out.ends_with('}') {
            line_out.push(';');
        }
        out.push_str("  ");
        out.push_str(&line_out);
        out.push('\n');
    }
    out
}

/// Build a project directory into `build/`.
pub fn build_project(dir: &Path) -> Result<(), String> {
    let out_dir = dir.join("build");
    fs::create_dir_all(&out_dir).map_err(|e| e.to_string())?;

    // Parse the entry page.
    let page_path = find_entry_page(dir).ok_or("no .wpf entry page found")?;
    let source = fs::read_to_string(&page_path).map_err(|e| e.to_string())?;
    let file = wpf::parse(&source).map_err(|e| e.to_string())?;
    let page = file.pages.first().ok_or("no page declared")?;

    // CSS: concatenate all .css files.
    let mut css = String::new();
    for p in find_styles(dir) {
        if let Ok(c) = fs::read_to_string(&p) {
            css.push_str(&c);
            css.push('\n');
        }
    }
    fs::write(out_dir.join("styles.css"), css).map_err(|e| e.to_string())?;

    // JS: transpile all .wynx files.
    let mut js = String::new();
    if let Some(script_path) = find_script(dir) {
        if let Ok(src) = fs::read_to_string(&script_path) {
            js.push_str(&transpile_script(&src));
            js.push('\n');
        }
    }
    // Also transpile inline scripts from the .wpf.
    for page in &file.pages {
        for script in &page.scripts {
            if !script.source.trim().is_empty() {
                js.push_str(&transpile_script(&script.source));
                js.push('\n');
            }
        }
    }
    fs::write(out_dir.join("app.js"), js).map_err(|e| e.to_string())?;

    // HTML.
    let html = render_page(page, "styles.css", "app.js");
    fs::write(out_dir.join("index.html"), html).map_err(|e| e.to_string())?;

    // Copy the entry page name info.
    println!("wynxer: built {} -> {}", page_path.display(), out_dir.display());
    Ok(())
}

/// Check a project directory.
pub fn check_project(dir: &Path) -> Result<(), String> {
    let page_path = find_entry_page(dir).ok_or("no .wpf entry page found")?;
    let source = fs::read_to_string(&page_path).map_err(|e| e.to_string())?;
    let file = wpf::parse(&source).map_err(|e| e.to_string())?;
    for page in &file.pages {
        for content in &page.contents {
            if let wpf::Content::Element(e) = content {
                if !wpf::is_element_kind(&e.kind) {
                    return Err(format!("unknown element kind '{}'", e.kind));
                }
            }
        }
    }
    // Typecheck scripts: ensure every element handler references a known export.
    // (Lightweight: just parse, no full typecheck yet.)
    println!("wynxer: check passed for {}", page_path.display());
    Ok(())
}

/// A minimal HTTP dev server. Serves files from `dir`.
pub fn serve(dir: &Path, port: u16) -> Result<(), String> {
    use std::io::{Read, Write};
    use std::net::TcpListener;
    let listener = TcpListener::bind(format!("127.0.0.1:{}", port))
        .map_err(|e| e.to_string())?;
    println!("wynxer: serving {} on http://localhost:{}", dir.display(), port);
    println!("wynxer: press Ctrl+C to stop");
    for stream in listener.incoming() {
        let mut stream = match stream {
            Ok(s) => s,
            Err(_) => continue,
        };
        let mut buf = [0u8; 4096];
        let n = stream.read(&mut buf).unwrap_or(0);
        let req = String::from_utf8_lossy(&buf[..n]);
        let path = req.lines().next().unwrap_or("").split_whitespace().nth(1).unwrap_or("/");
        let path = path.trim_start_matches('/');
        let path = if path.is_empty() { "index.html" } else { path };
        let file_path = dir.join(path);
        let (status, body, ctype) = if file_path.is_file() {
            let body = fs::read(&file_path).unwrap_or_default();
            let ctype = match file_path.extension().and_then(|s| s.to_str()) {
                Some("html") => "text/html; charset=utf-8",
                Some("css") => "text/css; charset=utf-8",
                Some("js") => "application/javascript; charset=utf-8",
                Some("json") => "application/json; charset=utf-8",
                Some("wasm") => "application/wasm",
                Some("png") => "image/png",
                Some("jpg") | Some("jpeg") => "image/jpeg",
                Some("svg") => "image/svg+xml",
                _ => "application/octet-stream",
            };
            ("200 OK", body, ctype)
        } else {
            ("404 Not Found", b"Not found".to_vec(), "text/plain")
        };
        let header = format!(
            "HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
            status, ctype, body.len()
        );
        let _ = stream.write_all(header.as_bytes());
        let _ = stream.write_all(&body);
    }
    Ok(())
}
