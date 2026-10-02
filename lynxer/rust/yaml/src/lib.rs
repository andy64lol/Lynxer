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
use serde::de::{MapAccess, SeqAccess, Visitor};
use serde::{Deserialize, Deserializer};
use std::fmt;

/// The largest document `yamlParse` / `yamlValid` will read, to bound alias
/// expansion.
const MAX_INPUT: usize = 1 << 20;

fn parse_to_json(text: &str) -> Option<String> {
    if text.len() > MAX_INPUT {
        return None;
    }
    let value: YamlJsonValue = serde_yml::from_str(text).ok()?;
    let value = value.0;
    serde_json::to_string(&value).ok()
}

struct YamlJsonValue(serde_json::Value);

struct YamlJsonVisitor;

impl<'de> Visitor<'de> for YamlJsonVisitor {
    type Value = YamlJsonValue;

    fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        formatter.write_str("a YAML value representable in JSON with string mapping keys")
    }

    fn visit_unit<E>(self) -> Result<Self::Value, E> {
        Ok(YamlJsonValue(serde_json::Value::Null))
    }

    fn visit_none<E: serde::de::Error>(self) -> Result<Self::Value, E> {
        self.visit_unit()
    }

    fn visit_bool<E>(self, value: bool) -> Result<Self::Value, E> {
        Ok(YamlJsonValue(serde_json::Value::Bool(value)))
    }

    fn visit_i64<E>(self, value: i64) -> Result<Self::Value, E> {
        Ok(YamlJsonValue(serde_json::Value::Number(value.into())))
    }

    fn visit_u64<E>(self, value: u64) -> Result<Self::Value, E> {
        Ok(YamlJsonValue(serde_json::Value::Number(value.into())))
    }

    fn visit_f64<E: serde::de::Error>(self, value: f64) -> Result<Self::Value, E> {
        serde_json::Number::from_f64(value)
            .map(|number| YamlJsonValue(serde_json::Value::Number(number)))
            .ok_or_else(|| E::custom("non-finite YAML number cannot be represented in JSON"))
    }

    fn visit_str<E>(self, value: &str) -> Result<Self::Value, E> {
        Ok(YamlJsonValue(serde_json::Value::String(value.to_string())))
    }

    fn visit_string<E>(self, value: String) -> Result<Self::Value, E> {
        Ok(YamlJsonValue(serde_json::Value::String(value)))
    }

    fn visit_newtype_struct<D: Deserializer<'de>>(
        self,
        deserializer: D,
    ) -> Result<Self::Value, D::Error> {
        YamlJsonValue::deserialize(deserializer)
    }

    fn visit_seq<A: SeqAccess<'de>>(self, mut sequence: A) -> Result<Self::Value, A::Error> {
        let mut values = Vec::new();
        while let Some(value) = sequence.next_element::<YamlJsonValue>()? {
            values.push(value.0);
        }
        Ok(YamlJsonValue(serde_json::Value::Array(values)))
    }

    fn visit_map<A: MapAccess<'de>>(self, mut mapping: A) -> Result<Self::Value, A::Error> {
        let mut values = serde_json::Map::new();
        while let Some(key) = mapping.next_key::<StringKey>()? {
            let value = mapping.next_value::<YamlJsonValue>()?;
            values.insert(key.0, value.0);
        }
        Ok(YamlJsonValue(serde_json::Value::Object(values)))
    }
}

impl<'de> Deserialize<'de> for YamlJsonValue {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        deserializer.deserialize_any(YamlJsonVisitor)
    }
}

struct StringKey(String);

struct StringKeyVisitor;

impl<'de> Visitor<'de> for StringKeyVisitor {
    type Value = StringKey;

    fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        formatter.write_str("a string YAML mapping key")
    }

    fn visit_str<E>(self, value: &str) -> Result<Self::Value, E> {
        Ok(StringKey(value.to_string()))
    }

    fn visit_string<E>(self, value: String) -> Result<Self::Value, E> {
        Ok(StringKey(value))
    }
}

impl<'de> Deserialize<'de> for StringKey {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        deserializer.deserialize_any(StringKeyVisitor)
    }
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

    #[test]
    fn non_string_mapping_keys_fail_without_data_loss() {
        for text in [
            "1: numeric\n",
            "nested:\n  1: numeric\n",
            "? [a, b]\n: sequence\n",
            "? null\n: null-key\n",
        ] {
            assert!(parse_to_json(text).is_none(), "accepted {text:?}");
        }
    }
}
