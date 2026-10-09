//! Markdown rendering to HTML.
use lynxer_abi::{export_string, lynxer_module};
use pulldown_cmark::{html, Options, Parser};

const MAX_INPUT: usize = 16 << 20;
fn render(text: &str, flags: i64) -> Option<String> {
    if text.len() > MAX_INPUT { return None; }
    let mut options = Options::empty();
    if flags & 1 != 0 { options.insert(Options::ENABLE_TABLES | Options::ENABLE_FOOTNOTES | Options::ENABLE_STRIKETHROUGH | Options::ENABLE_TASKLISTS); }
    let parser = Parser::new_ext(text, options);
    let mut output = String::new();
    html::push_html(&mut output, parser);
    Some(output)
}
export_string!(markdown_render, args, { render(args.string(0), args.int(1)).unwrap_or_default() });
const OPS: &[(&str, &str, &str)] = &[("markdownRender", "markdown_render", "cdecl:cstring(...)"), ("markdownParse", "markdown_render", "cdecl:cstring(...)")];
lynxer_module!(OPS);
