//! Lynxer `ini` stdlib backend: parse and serialize INI documents.
//!
//! Documents cross the ABI as JSON strings, the shared structured-value bridge
//! (see `docs/native-module-abi.md`). The mapping is an object of sections, each
//! an object of string values; the unnamed leading section uses `""` as its key.
//! Values are strings in both directions. A failure is the scalar sentinel:
//! `""` for a document op, `false` for `iniValid`.

use ini::{Ini, LineSeparator, WriteOption};
use lynxer_abi::{export_int, export_string, lynxer_module};
use serde_json::{Map, Value};

fn parse_to_json(text: &str) -> Option<String> {
    let document = Ini::load_from_str(text).ok()?;
    let mut root = Map::new();
    for (section, properties) in document.iter() {
        let mut body = Map::new();
        for (key, value) in properties.iter() {
            body.insert(key.to_string(), Value::String(value.to_string()));
        }
        root.insert(section.unwrap_or("").to_string(), Value::Object(body));
    }
    Some(Value::Object(root).to_string())
}

fn json_to_ini(json: &str) -> Option<String> {
    let value: Value = serde_json::from_str(json).ok()?;
    let sections = value.as_object()?;
    let mut document = Ini::new();
    for (section, body) in sections {
        let body = body.as_object()?;
        let section = if section.is_empty() {
            None
        } else {
            Some(section.as_str())
        };
        for (key, value) in body {
            let text = match value {
                Value::String(text) => text.clone(),
                other => other.to_string(),
            };
            document.with_section(section).set(key, text);
        }
    }
    // `write_to` uses rust-ini's `SystemDefault` line separator, which is CRLF
    // on Windows and LF elsewhere. Write LF explicitly so a serialized document
    // is byte-identical on every host: the fixtures compare it exactly, and the
    // interpreter's own output is LF everywhere.
    let option = WriteOption {
        line_separator: LineSeparator::CR,
        ..WriteOption::default()
    };
    let mut output = Vec::new();
    document.write_to_opt(&mut output, option).ok()?;
    String::from_utf8(output).ok()
}

fn get(text: &str, section: &str, key: &str) -> Option<String> {
    let value: Value = serde_json::from_str(&parse_to_json(text)?).ok()?;
    let found = value.get(section)?.get(key)?;
    Some(match found {
        Value::String(text) => text.clone(),
        other => other.to_string(),
    })
}

export_string!(ini_parse, args, {
    parse_to_json(args.string(0)).unwrap_or_default()
});

export_string!(ini_serialize, args, {
    json_to_ini(args.string(0)).unwrap_or_default()
});

export_int!(ini_valid, args, {
    Ini::load_from_str(args.string(0)).is_ok() as i64
});

export_string!(ini_get, args, {
    get(args.string(0), args.string(1), args.string(2)).unwrap_or_default()
});

const OPS: &[(&str, &str, &str)] = &[
    ("iniParse", "ini_parse", "cdecl:cstring(...)"),
    ("iniSerialize", "ini_serialize", "cdecl:cstring(...)"),
    ("iniValid", "ini_valid", "cdecl:int64(...)"),
    ("iniGet", "ini_get", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip() {
        let json = parse_to_json("[server]\nhost = localhost\nport = 8080\n").unwrap();
        assert!(json.contains("\"server\""));
        let text = json_to_ini(&json).unwrap();
        assert!(text.contains("host=localhost") || text.contains("host = localhost"));
    }

    #[test]
    fn get_reads_a_key() {
        assert_eq!(get("[a]\nb = c\n", "a", "b").as_deref(), Some("c"));
    }
}
