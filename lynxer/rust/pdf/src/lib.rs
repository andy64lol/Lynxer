//! PDF generation and extraction.
use base64::{engine::general_purpose::STANDARD, Engine};
use lynxer_abi::{export_string, lynxer_module};
use printpdf::{BuiltinFont, Mm, PdfDocument};

const MAX_INPUT: usize = 8 << 20;
fn generate(text: &str) -> Option<String> {
    if text.len() > MAX_INPUT { return None; }
    let (doc, page, layer) = PdfDocument::new("Lynxer document", Mm(210.0), Mm(297.0), "Text");
    let font = doc.add_builtin_font(BuiltinFont::Helvetica).ok()?;
    let current = doc.get_page(page).get_layer(layer);
    for (idx, line) in text.lines().enumerate() {
        if idx >= 50 { break; }
        current.use_text(line, 12.0, Mm(15.0), Mm(280.0 - idx as f32 * 5.0), &font);
    }
    let mut bytes = Vec::new();
    doc.save(&mut std::io::BufWriter::new(&mut bytes)).ok()?;
    Some(STANDARD.encode(bytes))
}
fn extract(input: &str) -> Option<String> {
    if input.len() > MAX_INPUT * 2 { return None; }
    let bytes = STANDARD.decode(input).ok()?;
    let doc = lopdf::Document::load_mem(&bytes).ok()?;
    let pages = doc.get_pages().keys().copied().collect::<Vec<_>>();
    doc.extract_text(&pages).ok()
}
export_string!(pdf_generate, args, { generate(args.string(0)).unwrap_or_default() });
export_string!(pdf_text, args, { extract(args.string(0)).unwrap_or_default() });
export_string!(pdf_info, args, {
    let result = STANDARD.decode(args.string(0)).ok().and_then(|b| lopdf::Document::load_mem(&b).ok()).map(|d| serde_json::json!({"pages": d.get_pages().len(), "version": d.version})).and_then(|v| serde_json::to_string(&v).ok());
    result.unwrap_or_default()
});
const OPS: &[(&str, &str, &str)] = &[("pdfGenerate", "pdf_generate", "cdecl:cstring(...)"), ("pdfText", "pdf_text", "cdecl:cstring(...)"), ("pdfInfo", "pdf_info", "cdecl:cstring(...)")];
lynxer_module!(OPS);
