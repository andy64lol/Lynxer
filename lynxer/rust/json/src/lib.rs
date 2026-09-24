//! Lynxer `json` stdlib backend, reimplemented on `serde_json`.
//!
//! Structured values cross the native ABI as JSON strings; the `.lynx` wrapper
//! only forwards, except for list-building helpers implemented with builtins.
//!
//! Output must match the previous `nlohmann::ordered_json` backend byte for
//! byte because `examples/stdlib_json.expected` diffs exactly:
//!   * objects keep key insertion order (`preserve_order`);
//!   * `parse` uses a 2-space indent and `pretty` a 4-space indent, with
//!     `": "` / `,\n` separators (which is what nlohmann's `dump(n)` uses);
//!   * the compact helpers use `": "` and `", "`.

use lynxer_abi::{export_float, export_int, export_string, clynxer_module};
use serde::Serialize;
use serde_json::Value;

fn parse(text: &str) -> Option<Value> {
    serde_json::from_str::<Value>(text).ok()
}

/// Reproduces the previous `compactDump`: serialize compactly, then put one
/// space after every `:` and `,` that appears outside a string.
fn compact(value: &Value) -> String {
    let raw = serde_json::to_string(value).unwrap_or_default();
    let mut output = String::with_capacity(raw.len() + raw.len() / 8);
    let mut in_string = false;
    let mut escaped = false;
    for character in raw.chars() {
        if in_string {
            output.push(character);
            if escaped {
                escaped = false;
            } else if character == '\\' {
                escaped = true;
            } else if character == '"' {
                in_string = false;
            }
            continue;
        }
        match character {
            '"' => {
                in_string = true;
                output.push(character);
            }
            ':' | ',' => {
                output.push(character);
                output.push(' ');
            }
            _ => output.push(character),
        }
    }
    output
}

/// Pretty-prints with the given indent, matching nlohmann's `dump(n)`.
fn pretty(value: &Value, indent: &[u8]) -> String {
    let mut buffer = Vec::new();
    let formatter = serde_json::ser::PrettyFormatter::with_indent(indent);
    let mut serializer = serde_json::Serializer::with_formatter(&mut buffer, formatter);
    if value.serialize(&mut serializer).is_err() {
        return String::new();
    }
    String::from_utf8(buffer).unwrap_or_default()
}

fn scalar_text(value: &Value) -> String {
    match value {
        Value::Null => String::new(),
        Value::Bool(flag) => {
            if *flag {
                "true".to_string()
            } else {
                "false".to_string()
            }
        }
        Value::String(text) => text.clone(),
        other => compact(other),
    }
}

fn find_field<'a>(value: &'a Value, key: &str) -> Option<&'a Value> {
    value.as_object().and_then(|object| object.get(key))
}

fn truthy(value: &Value) -> bool {
    match value {
        Value::Null => false,
        Value::Bool(flag) => *flag,
        Value::Number(number) => number.as_f64().map(|value| value != 0.0).unwrap_or(false),
        Value::String(text) => !text.is_empty(),
        Value::Array(items) => !items.is_empty(),
        Value::Object(fields) => !fields.is_empty(),
    }
}

/// `strtoll`-like parse: the whole (trimmed) string must be an integer.
fn parse_int(text: &str) -> i64 {
    trim(text).parse::<i64>().unwrap_or(0)
}

/// `strtod`-like parse: the whole (trimmed) string must be a number.
fn parse_float(text: &str) -> f64 {
    trim(text).parse::<f64>().unwrap_or(0.0)
}

/// The whitespace set nlohmann's `isSpace` uses (which includes `\v`).
fn is_space(character: char) -> bool {
    matches!(character, ' ' | '\t' | '\n' | '\r' | '\u{0C}' | '\u{0B}')
}

fn trim(text: &str) -> &str {
    text.trim_matches(is_space)
}

// --- ops --------------------------------------------------------------------

export_int!(json_valid, args, { parse(args.string(0)).is_some() as i64 });

export_string!(json_parse, args, {
    match parse(args.string(0)) {
        Some(value) => pretty(&value, b"  "),
        None => String::new(),
    }
});

export_string!(json_pretty, args, {
    match parse(args.string(0)) {
        Some(value) => pretty(&value, b"    "),
        None => String::new(),
    }
});

export_string!(json_get, args, {
    match parse(args.string(0))
        .as_ref()
        .and_then(|value| find_field(value, args.string(1)))
    {
        Some(found) => scalar_text(found),
        None => String::new(),
    }
});

export_int!(json_get_int, args, {
    let value = match parse(args.string(0)) {
        Some(value) => value,
        None => return 0,
    };
    let found = match find_field(&value, args.string(1)) {
        Some(found) => found,
        None => return 0,
    };
    match found {
        Value::Number(number) => number.as_f64().map(|value| value as i64).unwrap_or(0),
        Value::Bool(flag) => *flag as i64,
        Value::String(text) => parse_int(text),
        _ => 0,
    }
});

export_float!(json_get_float, args, {
    let value = match parse(args.string(0)) {
        Some(value) => value,
        None => return 0.0,
    };
    let found = match find_field(&value, args.string(1)) {
        Some(found) => found,
        None => return 0.0,
    };
    match found {
        Value::Number(number) => number.as_f64().unwrap_or(0.0),
        Value::Bool(flag) => {
            if *flag {
                1.0
            } else {
                0.0
            }
        }
        Value::String(text) => parse_float(text),
        _ => 0.0,
    }
});

export_int!(json_get_bool, args, {
    match parse(args.string(0))
        .as_ref()
        .and_then(|value| find_field(value, args.string(1)))
    {
        Some(found) => truthy(found) as i64,
        None => 0,
    }
});

export_string!(json_keys, args, {
    match parse(args.string(0)) {
        Some(Value::Object(fields)) => fields.keys().cloned().collect::<Vec<String>>().join(","),
        _ => String::new(),
    }
});

export_string!(json_stringify, args, {
    serde_json::to_string(&Value::String(args.string(0).to_string())).unwrap_or_default()
});

export_int!(json_has, args, {
    let wanted = args.string(1);
    match parse(args.string(0)) {
        Some(Value::Object(fields)) => fields.contains_key(wanted) as i64,
        Some(Value::Array(items)) => items.iter().any(|item| item.as_str() == Some(wanted)) as i64,
        _ => 0,
    }
});

export_int!(json_length, args, {
    match parse(args.string(0)) {
        Some(Value::Object(fields)) => fields.len() as i64,
        Some(Value::Array(items)) => items.len() as i64,
        // nlohmann's size() returns 1 for scalar values, including strings.
        Some(Value::String(_)) => 1,
        _ => 0,
    }
});

export_string!(json_set, args, {
    let text = args.string(0);
    let key = args.string(1);
    let value = args.string(2);
    let object = match parse(text) {
        Some(Value::Object(fields)) => fields,
        _ => return text.to_string(),
    };
    let mut object = object;
    match parse(value) {
        Some(parsed) => {
            object.insert(key.to_string(), parsed);
        }
        None => {
            object.insert(key.to_string(), Value::String(value.to_string()));
        }
    }
    compact(&Value::Object(object))
});

export_string!(json_set_int, args, {
    let text = args.string(0);
    let key = args.string(1);
    let mut object = match parse(text) {
        Some(Value::Object(fields)) => fields,
        _ => return text.to_string(),
    };
    object.insert(
        key.to_string(),
        Value::Number(serde_json::Number::from(args.int(0))),
    );
    compact(&Value::Object(object))
});

export_string!(json_delete, args, {
    let text = args.string(0);
    let key = args.string(1);
    let mut object = match parse(text) {
        Some(Value::Object(fields)) => fields,
        _ => return text.to_string(),
    };
    object.remove(key);
    compact(&Value::Object(object))
});

export_string!(json_merge, args, {
    let first = args.string(0);
    let second = args.string(1);
    let mut left = match parse(first) {
        Some(Value::Object(fields)) => fields,
        _ => return first.to_string(),
    };
    let right = match parse(second) {
        Some(Value::Object(fields)) => fields,
        _ => return first.to_string(),
    };
    for (key, value) in right.iter() {
        left.insert(key.clone(), value.clone());
    }
    compact(&Value::Object(left))
});

export_string!(json_type, args, {
    let value = match parse(args.string(0)) {
        Some(Value::Object(fields)) => fields,
        _ => return "unknown".to_string(),
    };
    let found = match value.get(args.string(1)) {
        Some(found) => found,
        None => return "null".to_string(),
    };
    match found {
        Value::Null => "null",
        Value::Bool(_) => "bool",
        Value::Number(number) => {
            if number.is_f64() {
                "float"
            } else {
                "int"
            }
        }
        Value::String(_) => "string",
        Value::Array(_) => "array",
        Value::Object(_) => "object",
    }
    .to_string()
});

export_string!(json_build, args, {
    let input = args.string(0);
    let mut object = serde_json::Map::new();
    let mut start = 0usize;
    loop {
        let separator = input[start..].find('|').map(|offset| start + offset);
        let end = separator.unwrap_or(input.len());
        let pair = trim(&input[start..end]);
        if let Some(equals) = pair.find('=') {
            let key = trim(&pair[..equals]);
            let value = trim(&pair[equals + 1..]);
            object.insert(key.to_string(), Value::String(value.to_string()));
        }
        match separator {
            Some(position) => start = position + 1,
            None => break,
        }
        if start > input.len() {
            break;
        }
    }
    compact(&Value::Object(object))
});

const OPS: &[(&str, &str, &str)] = &[
    ("valid", "json_valid", "cdecl:int64(...)"),
    ("parse", "json_parse", "cdecl:cstring(...)"),
    ("pretty", "json_pretty", "cdecl:cstring(...)"),
    ("get", "json_get", "cdecl:cstring(...)"),
    ("getInt", "json_get_int", "cdecl:int64(...)"),
    ("getFloat", "json_get_float", "cdecl:float64(...)"),
    ("getBool", "json_get_bool", "cdecl:int64(...)"),
    ("keys", "json_keys", "cdecl:cstring(...)"),
    ("stringify", "json_stringify", "cdecl:cstring(...)"),
    ("has", "json_has", "cdecl:int64(...)"),
    ("length", "json_length", "cdecl:int64(...)"),
    ("set", "json_set", "cdecl:cstring(...)"),
    ("setInt", "json_set_int", "cdecl:cstring(...)"),
    ("delete", "json_delete", "cdecl:cstring(...)"),
    ("merge", "json_merge", "cdecl:cstring(...)"),
    ("type", "json_type", "cdecl:cstring(...)"),
    ("build", "json_build", "cdecl:cstring(...)"),
];

clynxer_module!(OPS);
