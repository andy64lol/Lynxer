//! Lynxer `toml` stdlib backend: parse and serialize TOML documents.
//!
//! Documents cross the ABI as JSON strings, the shared structured-value bridge
//! (see `docs/native-module-abi.md`). A failure is the scalar sentinel: `""`
//! for a document op, `false` for `tomlValid`. TOML datetimes use the
//! `$lynxer.toml.datetime` single-field JSON object representation so their
//! type survives parsing and serialization.

use lynxer_abi::{export_int, export_string, lynxer_module};
use serde_json::{Number, Value};

const DATETIME_TAG: &str = "$lynxer.toml.datetime";

fn toml_to_json(value: toml::Value) -> Value {
    match value {
        toml::Value::Datetime(datetime) => {
            serde_json::json!({ DATETIME_TAG: datetime.to_string() })
        }
        toml::Value::Array(values) => Value::Array(values.into_iter().map(toml_to_json).collect()),
        toml::Value::Table(values) => Value::Object(
            values
                .into_iter()
                .map(|(key, value)| (key, toml_to_json(value)))
                .collect(),
        ),
        toml::Value::String(value) => Value::String(value),
        toml::Value::Integer(value) => Value::Number(Number::from(value)),
        toml::Value::Float(value) => Number::from_f64(value)
            .map(Value::Number)
            .unwrap_or(Value::Null),
        toml::Value::Boolean(value) => Value::Bool(value),
    }
}

fn datetime_from_json(value: &Value) -> Option<toml::Value> {
    let fields = value.as_object()?;
    if fields.len() != 1 {
        return None;
    }
    let text = fields.get(DATETIME_TAG)?.as_str()?;
    let document: toml::Table = toml::from_str(&format!("value = {text}\n")).ok()?;
    match document.get("value")? {
        toml::Value::Datetime(datetime) => Some(toml::Value::Datetime(*datetime)),
        _ => None,
    }
}

fn json_to_toml_value(value: Value) -> Option<toml::Value> {
    if let Some(datetime) = datetime_from_json(&value) {
        return Some(datetime);
    }
    Some(match value {
        Value::Null => return None,
        Value::Bool(value) => toml::Value::Boolean(value),
        Value::Number(value) => {
            if let Some(integer) = value.as_i64() {
                toml::Value::Integer(integer)
            } else if let Some(integer) = value.as_u64() {
                toml::Value::Integer(i64::try_from(integer).ok()?)
            } else {
                toml::Value::Float(value.as_f64()?)
            }
        }
        Value::String(value) => toml::Value::String(value),
        Value::Array(values) => toml::Value::Array(
            values
                .into_iter()
                .map(json_to_toml_value)
                .collect::<Option<Vec<_>>>()?,
        ),
        Value::Object(values) => {
            let mut table = toml::Table::new();
            for (key, value) in values {
                table.insert(key, json_to_toml_value(value)?);
            }
            toml::Value::Table(table)
        }
    })
}

fn parse_to_json(text: &str) -> Option<String> {
    let value: toml::Value = toml::from_str(text).ok()?;
    serde_json::to_string(&toml_to_json(value)).ok()
}

fn json_to_toml(json: &str) -> Option<String> {
    let value: Value = serde_json::from_str(json).ok()?;
    toml::to_string(&json_to_toml_value(value)?).ok()
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

    #[test]
    fn datetime_round_trip_preserves_toml_type_and_value() {
        let original = "date = 1979-05-27\nlocal_time = 07:32:00\nstamp = 1979-05-27T07:32:00Z\n";
        let json = parse_to_json(original).unwrap();
        assert!(json.contains(DATETIME_TAG));
        let serialized = json_to_toml(&json).unwrap();
        assert!(serialized.contains("date = 1979-05-27"));
        assert!(serialized.contains("local_time = 07:32:00"));
        assert!(serialized.contains("stamp = 1979-05-27T07:32:00Z"));
        assert_eq!(
            serde_json::from_str::<Value>(&parse_to_json(&serialized).unwrap()).unwrap(),
            serde_json::from_str::<Value>(&json).unwrap()
        );
    }
}
