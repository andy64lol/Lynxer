//! Lynxer `yaml` stdlib backend: parse and serialize YAML documents.
//!
//! Documents cross the ABI as JSON strings, the shared structured-value bridge
//! (see `docs/native-module-abi.md`). A YAML mapping becomes a JSON object and a
//! sequence a JSON array; a non-string mapping key (which JSON cannot hold) is a
//! failure. Anchors and aliases are resolved during parsing, so the input is
//! capped at 1 MiB to bound an alias-expansion payload.
//!
//! A failure is the scalar sentinel: `""` for a document op, `false` for
//! `yamlValid`.

use lynxer_abi::{export_int, export_string, lynxer_module};

/// The largest document `yamlParse` / `yamlValid` will read, to bound alias
/// expansion.
const MAX_INPUT: usize = 1 << 20;

fn parse_to_json(text: &str) -> Option<String> {
    if text.len() > MAX_INPUT {
        return None;
    }
    let value: serde_json::Value = serde_yml::from_str(text).ok()?;
    serde_json::to_string(&value).ok()
}

fn json_to_yaml(json: &str) -> Option<String> {
    let value: serde_json::Value = serde_json::from_str(json).ok()?;
    serde_yml::to_string(&value).ok()
}

fn json_get(value: &serde_json::Value, key: &str) -> Option<serde_json::Value> {
    let mut current = value;
    for part in key.split('.') {
        current = current.get(part)?;
    }
    Some(current.clone())
}

export_string!(yaml_parse, args, {
    parse_to_json(args.string(0)).unwrap_or_default()
});

export_string!(yaml_serialize, args, {
    json_to_yaml(args.string(0)).unwrap_or_default()
});

export_int!(yaml_valid, args, {
    parse_to_json(args.string(0)).is_some() as i64
});

export_string!(yaml_get, args, {
    match parse_to_json(args.string(0)).and_then(|json| serde_json::from_str(&json).ok()) {
        Some(value) => match json_get(&value, args.string(1)) {
            Some(found) => serde_json::to_string(&found).unwrap_or_default(),
            None => String::new(),
        },
        None => String::new(),
    }
});

const OPS: &[(&str, &str, &str)] = &[
    ("yamlParse", "yaml_parse", "cdecl:cstring(...)"),
    ("yamlSerialize", "yaml_serialize", "cdecl:cstring(...)"),
    ("yamlValid", "yaml_valid", "cdecl:int64(...)"),
    ("yamlGet", "yaml_get", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip() {
        let json = parse_to_json("name: lynxer\nitems:\n  - 1\n  - 2\n").unwrap();
        assert!(json.contains("\"name\":\"lynxer\""));
        let yaml = json_to_yaml(&json).unwrap();
        assert!(yaml.contains("lynxer"));
    }

    #[test]
    fn nested_get() {
        let json = parse_to_json("a:\n  b: 7\n").unwrap();
        let value: serde_json::Value = serde_json::from_str(&json).unwrap();
        assert_eq!(json_get(&value, "a.b").unwrap(), serde_json::json!(7));
    }
}
