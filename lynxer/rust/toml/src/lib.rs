//! Lynxer `toml` stdlib backend: parse and serialize TOML documents.
//!
//! Documents cross the ABI as JSON strings, the shared structured-value bridge
//! (see `docs/native-module-abi.md`). A failure is the scalar sentinel: `""`
//! for a document op, `false` for `tomlValid`.

use lynxer_abi::{export_int, export_string, lynxer_module};

fn parse_to_json(text: &str) -> Option<String> {
    let value: serde_json::Value = toml::from_str(text).ok()?;
    Some(serde_json::to_string(&value).unwrap_or_default())
}

fn json_to_toml(json: &str) -> Option<String> {
    let value: serde_json::Value = serde_json::from_str(json).ok()?;
    toml::to_string(&value).ok()
}

/// Walk a dotted key through a JSON value.
fn json_get(value: &serde_json::Value, key: &str) -> Option<serde_json::Value> {
    let mut current = value;
    for part in key.split('.') {
        current = current.get(part)?;
    }
    Some(current.clone())
}

export_string!(toml_parse, args, {
    parse_to_json(args.string(0)).unwrap_or_default()
});

export_string!(toml_serialize, args, {
    json_to_toml(args.string(0)).unwrap_or_default()
});

export_int!(toml_valid, args, {
    toml::from_str::<serde_json::Value>(args.string(0)).is_ok() as i64
});

export_string!(toml_get, args, {
    match toml::from_str::<serde_json::Value>(args.string(0)) {
        Ok(value) => match json_get(&value, args.string(1)) {
            Some(found) => serde_json::to_string(&found).unwrap_or_default(),
            None => String::new(),
        },
        Err(_) => String::new(),
    }
});

const OPS: &[(&str, &str, &str)] = &[
    ("tomlParse", "toml_parse", "cdecl:cstring(...)"),
    ("tomlSerialize", "toml_serialize", "cdecl:cstring(...)"),
    ("tomlValid", "toml_valid", "cdecl:int64(...)"),
    ("tomlGet", "toml_get", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip() {
        let json = parse_to_json("answer = 42\nname = \"lynxer\"\n").unwrap();
        assert!(json.contains("\"answer\":42"));
        let toml = json_to_toml(&json).unwrap();
        assert!(toml.contains("answer = 42"));
    }

    #[test]
    fn invalid_is_sentinel() {
        assert!(parse_to_json("= = =").is_none());
    }
}
