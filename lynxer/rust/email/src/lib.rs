//! MIME parsing and construction for Lynxer.
use base64::{engine::general_purpose::STANDARD, Engine};
use lynxer_abi::{export_string, lynxer_module};
use mailparse::parse_mail;
use serde_json::{json, Value};

const MAX_MESSAGE: usize = 16 << 20;

fn parse_message(raw: &str) -> Option<String> {
    if raw.len() > MAX_MESSAGE { return None; }
    let mail = parse_mail(raw.as_bytes()).ok()?;
    let headers = mail.headers.iter().map(|h| (h.get_key().to_ascii_lowercase(), Value::String(h.get_value()))).collect::<serde_json::Map<_,_>>();
    let mut parts = Vec::new();
    fn walk(part: &mailparse::ParsedMail<'_>, out: &mut Vec<Value>) -> Option<()> {
        let ctype = part.ctype.mimetype.clone();
        if !part.subparts.is_empty() {
            for child in &part.subparts { walk(child, out)?; }
        } else {
            let bytes = part.get_body_raw().ok()?;
            let disposition = part.get_content_disposition();
            let filename = disposition.params.get("filename").cloned();
            let body = if ctype.starts_with("text/") && filename.is_none() { Value::String(part.get_body().ok()?) } else { json!({"base64": STANDARD.encode(bytes)}) };
            out.push(json!({"contentType": ctype, "filename": filename, "body": body}));
        }
        Some(())
    }
    walk(&mail, &mut parts)?;
    serde_json::to_string(&json!({"headers": headers, "parts": parts})).ok()
}

fn build(input: &str) -> Option<String> {
    if input.len() > MAX_MESSAGE { return None; }
    let v: Value = serde_json::from_str(input).ok()?;
    let from = v["from"].as_str()?;
    let to = v["to"].as_str()?;
    let subject = v["subject"].as_str().unwrap_or("");
    let safe = |s: &str| !s.contains('\r') && !s.contains('\n');
    if ![from, to, subject].iter().all(|s| safe(s)) { return None; }
    let headers = v.get("headers").and_then(Value::as_object);
    if headers.is_some_and(|h| h.iter().any(|(k, val)| {
        k.is_empty() || !k.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-')
            || ["from", "to", "subject", "mime-version", "content-type"].contains(&k.to_ascii_lowercase().as_str())
            || !val.as_str().is_some_and(safe)
    })) { return None; }
    let attachments = v["attachments"].as_array().cloned().unwrap_or_default();
    let boundary = format!("lynxer-{:x}-{:x}", input.len(), std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).ok()?.as_nanos());
    let mut output = format!("From: {from}\r\nTo: {to}\r\nSubject: {subject}\r\nMIME-Version: 1.0\r\n");
    if let Some(headers) = headers {
        for (name, value) in headers { output.push_str(&format!("{name}: {}\r\n", value.as_str()?)); }
    }
    output.push_str(&format!("Content-Type: multipart/mixed; boundary=\"{boundary}\"\r\n\r\n"));
    for (field, mime) in [("text", "text/plain; charset=utf-8"), ("html", "text/html; charset=utf-8")] {
        if let Some(body) = v[field].as_str() {
            output.push_str(&format!("--{boundary}\r\nContent-Type: {mime}\r\nContent-Transfer-Encoding: base64\r\n\r\n"));
            for chunk in STANDARD.encode(body.as_bytes()).as_bytes().chunks(76) {
                output.push_str(std::str::from_utf8(chunk).ok()?); output.push_str("\r\n");
            }
        }
    }
    for attachment in attachments {
        let filename = attachment["filename"].as_str()?;
        let content_type = attachment["contentType"].as_str().unwrap_or("application/octet-stream");
        if !safe(filename) || !safe(content_type) { return None; }
        let bytes = STANDARD.decode(attachment["base64"].as_str()?).ok()?;
        output.push_str(&format!("--{boundary}\r\nContent-Type: {content_type}; name=\"{filename}\"\r\nContent-Disposition: attachment; filename=\"{filename}\"\r\nContent-Transfer-Encoding: base64\r\n\r\n"));
        for chunk in STANDARD.encode(bytes).as_bytes().chunks(76) {
            output.push_str(std::str::from_utf8(chunk).ok()?); output.push_str("\r\n");
        }
    }
    output.push_str(&format!("--{boundary}--\r\n"));
    (output.len() <= MAX_MESSAGE).then_some(output)
}

export_string!(email_parse, args, { parse_message(args.string(0)).unwrap_or_default() });
export_string!(email_build, args, { build(args.string(0)).unwrap_or_default() });
const OPS: &[(&str, &str, &str)] = &[("emailParse", "email_parse", "cdecl:cstring(...)"), ("emailBuild", "email_build", "cdecl:cstring(...)")];
lynxer_module!(OPS);
