//! Lynxer `xml` stdlib backend: parse and serialize XML documents.
//!
//! A document maps onto a JSON element tree, the shared structured-value bridge
//! (see `docs/native-module-abi.md`):
//!
//! ```json
//! {"name":"root","attributes":{"a":"1"},"text":"hi","children":[ ... ]}
//! ```
//!
//! `text` is the character data directly inside the element; each child is
//! another element. Comments, processing instructions and the doctype are
//! ignored, and no external entity is ever resolved. A failure is the scalar
//! sentinel: `""` for a document op, `false` for `xmlValid`.

use lynxer_abi::{export_int, export_string, lynxer_module};
use quick_xml::events::{BytesStart, Event};
use quick_xml::Reader;
use serde_json::{json, Map, Value};

/// The largest document `xmlParse` / `xmlValid` will read, so a huge input
/// cannot balloon into a proportionally larger JSON tree.
const MAX_INPUT: usize = 16 << 20;

#[derive(Default)]
struct Frame {
    name: String,
    attributes: Map<String, Value>,
    text: String,
    children: Vec<Value>,
}

fn frame_of(element: &BytesStart) -> Option<Frame> {
    let mut attributes = Map::new();
    for attribute in element.attributes() {
        let attribute = attribute.ok()?;
        let key = attribute.key.as_ref().to_string();
        let value = attribute.normalized_value(quick_xml::XmlVersion::Implicit1_0).ok()?.into_owned();
        attributes.insert(key, Value::String(value));
    }
    Some(Frame {
        name: element.local_name().as_ref().to_string(),
        attributes,
        text: String::new(),
        children: Vec::new(),
    })
}

fn frame_value(frame: Frame) -> Value {
    json!({
        "name": frame.name,
        "attributes": Value::Object(frame.attributes),
        "text": frame.text,
        "children": frame.children,
    })
}

fn parse_to_json(text: &str) -> Option<String> {
    if text.len() > MAX_INPUT {
        return None;
    }
    let mut reader = Reader::from_str(text);
    reader.config_mut().trim_text(false);
    let mut stack: Vec<Frame> = Vec::new();
    let mut root: Option<Value> = None;
    loop {
        match reader.read_event() {
            Ok(Event::Start(element)) => stack.push(frame_of(&element)?),
            Ok(Event::Empty(element)) => {
                let value = frame_value(frame_of(&element)?);
                match stack.last_mut() {
                    Some(top) => top.children.push(value),
                    None => root = Some(value),
                }
            }
            Ok(Event::Text(text)) => {
                if let Some(top) = stack.last_mut() {
                    top.text.push_str(text.xml10_content().as_ref());
                }
            }
            Ok(Event::CData(data)) => {
                if let Some(top) = stack.last_mut() {
                    top.text.push_str(data.as_ref());
                }
            }
            Ok(Event::End(_)) => {
                let value = frame_value(stack.pop()?);
                match stack.last_mut() {
                    Some(top) => top.children.push(value),
                    None => root = Some(value),
                }
            }
            Ok(Event::Eof) => break,
            Ok(_) => {}
            Err(_) => return None,
        }
    }
    if !stack.is_empty() {
        return None;
    }
    Some(serde_json::to_string(&root?).unwrap_or_default())
}

fn escape_text(text: &str, out: &mut String) {
    out.push_str(&quick_xml::escape::escape(text));
}

fn write_element(value: &Value, out: &mut String) {
    let name = value
        .get("name")
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_string();
    out.push('<');
    out.push_str(&name);
    if let Some(attributes) = value.get("attributes").and_then(Value::as_object) {
        for (key, attribute) in attributes {
            out.push(' ');
            out.push_str(key);
            out.push_str("=\"");
            escape_text(&attribute.as_str().unwrap_or("").to_string(), out);
            out.push('"');
        }
    }
    let text = value.get("text").and_then(Value::as_str).unwrap_or("");
    let children = value.get("children").and_then(Value::as_array);
    let has_children = children.map(|list| !list.is_empty()).unwrap_or(false);
    if text.is_empty() && !has_children {
        out.push_str("/>");
        return;
    }
    out.push('>');
    escape_text(text, out);
    if let Some(children) = children {
        for child in children {
            write_element(child, out);
        }
    }
    out.push_str("</");
    out.push_str(&name);
    out.push('>');
}

fn json_to_xml(json: &str) -> Option<String> {
    if json.len() > MAX_INPUT {
        return None;
    }
    let value: Value = serde_json::from_str(json).ok()?;
    if value.get("name").and_then(Value::as_str).is_none() {
        return None;
    }
    let mut output = String::new();
    write_element(&value, &mut output);
    Some(output)
}

export_string!(xml_parse, args, {
    parse_to_json(args.string(0)).unwrap_or_default()
});

export_string!(xml_serialize, args, {
    json_to_xml(args.string(0)).unwrap_or_default()
});

export_int!(xml_valid, args, {
    parse_to_json(args.string(0)).is_some() as i64
});

export_string!(xml_escape, args, {
    quick_xml::escape::escape(args.string(0)).into_owned()
});

export_string!(xml_unescape, args, {
    match quick_xml::escape::unescape(args.string(0)) {
        Ok(text) => text.into_owned(),
        Err(_) => String::new(),
    }
});

const OPS: &[(&str, &str, &str)] = &[
    ("xmlParse", "xml_parse", "cdecl:cstring(...)"),
    ("xmlSerialize", "xml_serialize", "cdecl:cstring(...)"),
    ("xmlValid", "xml_valid", "cdecl:int64(...)"),
    ("xmlEscape", "xml_escape", "cdecl:cstring(...)"),
    ("xmlUnescape", "xml_unescape", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip() {
        let json = parse_to_json("<a x=\"1\">hi<b/></a>").unwrap();
        assert!(json.contains("\"name\":\"a\""));
        let xml = json_to_xml(&json).unwrap();
        assert_eq!(xml, "<a x=\"1\">hi<b/></a>");
    }

    #[test]
    fn malformed_is_sentinel() {
        assert!(parse_to_json("<a><b></a>").is_none());
    }
}
