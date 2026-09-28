//! Lynxer `uuid` stdlib backend: UUID generation, parsing and formatting.
//!
//! UUIDs cross the ABI as strings, because Lynxer has no byte type. The
//! canonical form is the lower-case hyphenated one; the "simple" form (32 hex
//! characters, no hyphens) is the hex view of the 16 bytes, which is what
//! `uuidToHex` returns and `uuidFromHex` accepts.
//!
//! Parsing accepts every form the `uuid` crate does — hyphenated, simple,
//! `urn:uuid:` and braced — and surrounding whitespace is trimmed first.
//! Failures are in-band with the scalar-sentinel family documented in
//! `docs/stdlib-contracts.md`: an unparseable UUID yields `""` from a string
//! operation and `-1` from `uuidTimestamp` or `uuidVersion`.
//!
//! v4 is random and v7 is time-ordered, so neither is reproducible; v3 and v5
//! hash a name into a namespace and are. v1 and v6 are deliberately absent, as
//! are the byte-buffer entry points the lack of a byte type would make awkward.

use lynxer_abi::{export_int, export_string, lynxer_module};
use uuid::{Uuid, Variant};

/// Parses any form the crate accepts, after trimming surrounding whitespace.
fn parse(text: &str) -> Option<Uuid> {
    Uuid::parse_str(text.trim()).ok()
}

/// The canonical lower-case hyphenated form.
fn canonical(uuid: &Uuid) -> String {
    uuid.hyphenated().to_string()
}

/// The RFC name of a variant. `Variant` is `#[non_exhaustive]`, so a future
/// variant reports `"unknown"` rather than failing.
fn variant_name(variant: Variant) -> &'static str {
    match variant {
        Variant::NCS => "ncs",
        Variant::RFC4122 => "rfc4122",
        Variant::Microsoft => "microsoft",
        Variant::Future => "future",
        _ => "unknown",
    }
}

// --- generation --------------------------------------------------------------

export_string!(uuid_v4, args, { canonical(&Uuid::new_v4()) });

export_string!(uuid_v7, args, { canonical(&Uuid::now_v7()) });

export_string!(uuid_v3, args, {
    match parse(args.string(0)) {
        Some(namespace) => canonical(&Uuid::new_v3(&namespace, args.string(1).as_bytes())),
        None => String::new(),
    }
});

export_string!(uuid_v5, args, {
    match parse(args.string(0)) {
        Some(namespace) => canonical(&Uuid::new_v5(&namespace, args.string(1).as_bytes())),
        None => String::new(),
    }
});

export_string!(uuid_nil, args, { canonical(&Uuid::nil()) });

// --- parsing and formatting --------------------------------------------------

export_string!(uuid_parse, args, {
    parse(args.string(0))
        .map(|uuid| canonical(&uuid))
        .unwrap_or_default()
});

export_string!(uuid_format, args, {
    let text = match parse(args.string(0)) {
        Some(uuid) => canonical(&uuid),
        None => return String::new(),
    };
    if args.int(0) != 0 {
        text.to_ascii_uppercase()
    } else {
        text
    }
});

export_int!(uuid_valid, args, { parse(args.string(0)).is_some() as i64 });

export_int!(uuid_version, args, {
    match parse(args.string(0)) {
        Some(uuid) => uuid.get_version_num() as i64,
        None => -1,
    }
});

export_string!(uuid_variant, args, {
    match parse(args.string(0)) {
        Some(uuid) => variant_name(uuid.get_variant()).to_string(),
        None => String::new(),
    }
});

export_int!(uuid_timestamp, args, {
    match parse(args.string(0)).and_then(|uuid| uuid.get_timestamp()) {
        Some(timestamp) => {
            let (seconds, nanos) = timestamp.to_unix();
            (seconds as i64) * 1000 + (nanos as i64) / 1_000_000
        }
        None => -1,
    }
});

export_string!(uuid_to_hex, args, {
    match parse(args.string(0)) {
        Some(uuid) => uuid.simple().to_string(),
        None => String::new(),
    }
});

export_string!(uuid_from_hex, args, {
    let text = args.string(0);
    if text.len() == 32 && text.bytes().all(|byte| byte.is_ascii_hexdigit()) {
        parse(text).map(|uuid| canonical(&uuid)).unwrap_or_default()
    } else {
        String::new()
    }
});

export_string!(uuid_namespace, args, {
    match args.string(0).to_ascii_lowercase().as_str() {
        "dns" => canonical(&Uuid::NAMESPACE_DNS),
        "url" => canonical(&Uuid::NAMESPACE_URL),
        "oid" => canonical(&Uuid::NAMESPACE_OID),
        "x500" => canonical(&Uuid::NAMESPACE_X500),
        _ => String::new(),
    }
});

const OPS: &[(&str, &str, &str)] = &[
    ("v4", "uuid_v4", "cdecl:cstring(...)"),
    ("v7", "uuid_v7", "cdecl:cstring(...)"),
    ("v3", "uuid_v3", "cdecl:cstring(...)"),
    ("v5", "uuid_v5", "cdecl:cstring(...)"),
    ("nil", "uuid_nil", "cdecl:cstring(...)"),
    ("parse", "uuid_parse", "cdecl:cstring(...)"),
    ("format", "uuid_format", "cdecl:cstring(...)"),
    ("valid", "uuid_valid", "cdecl:int64(...)"),
    ("version", "uuid_version", "cdecl:int64(...)"),
    ("variant", "uuid_variant", "cdecl:cstring(...)"),
    ("timestamp", "uuid_timestamp", "cdecl:int64(...)"),
    ("toHex", "uuid_to_hex", "cdecl:cstring(...)"),
    ("fromHex", "uuid_from_hex", "cdecl:cstring(...)"),
    ("namespace", "uuid_namespace", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn v4_is_random_and_well_formed() {
        let first = Uuid::new_v4();
        let second = Uuid::new_v4();
        assert_ne!(first, second);
        assert_eq!(first.get_version_num(), 4);
        assert_eq!(first.get_variant(), Variant::RFC4122);
        assert!(first.get_timestamp().is_none());
    }

    #[test]
    fn v7_is_time_ordered() {
        let first = Uuid::now_v7();
        let second = Uuid::now_v7();
        assert_eq!(first.get_version_num(), 7);
        assert!(
            second.get_timestamp().unwrap().to_unix().0
                >= first.get_timestamp().unwrap().to_unix().0
        );
    }

    #[test]
    fn v3_and_v5_match_the_rfc_examples() {
        // RFC 4122 / RFC 9562 name-based examples for www.example.com.
        assert_eq!(
            canonical(&Uuid::new_v3(&Uuid::NAMESPACE_DNS, b"www.example.com")),
            "5df41881-3aed-3515-88a7-2f4a814cf09e"
        );
        assert_eq!(
            canonical(&Uuid::new_v5(&Uuid::NAMESPACE_DNS, b"www.example.com")),
            "2ed6657d-e927-568b-95e1-2665a8aea6a2"
        );
    }

    #[test]
    fn every_input_form_parses_to_the_canonical_one() {
        let canonical_form = "a1a2a3a4-b1b2-c1c2-d1d2-d3d4d5d6d7d8";
        let simple_form = "a1a2a3a4b1b2c1c2d1d2d3d4d5d6d7d8";
        assert_eq!(canonical(&parse(canonical_form).unwrap()), canonical_form);
        assert_eq!(canonical(&parse(simple_form).unwrap()), canonical_form);
        assert_eq!(
            canonical(&parse(&format!("urn:uuid:{canonical_form}")).unwrap()),
            canonical_form
        );
        assert_eq!(
            canonical(&parse(&format!("{{{canonical_form}}}")).unwrap()),
            canonical_form
        );
        assert_eq!(
            canonical(&parse("  a1a2a3a4b1b2c1c2d1d2d3d4d5d6d7d8  ").unwrap()),
            canonical_form
        );
        // A URN or braced form has to wrap the hyphenated one, not the simple
        // one, so those two are rejected.
        assert!(parse(&format!("urn:uuid:{simple_form}")).is_none());
        assert!(parse(&format!("{{{simple_form}}}")).is_none());
        assert!(parse("nope").is_none());
        assert!(parse(&simple_form[..31]).is_none());
    }

    #[test]
    fn nil_reports_version_zero_and_the_ncs_variant() {
        assert_eq!(
            canonical(&Uuid::nil()),
            "00000000-0000-0000-0000-000000000000"
        );
        assert_eq!(Uuid::nil().get_version_num(), 0);
        assert_eq!(Uuid::nil().get_variant(), Variant::NCS);
        assert!(Uuid::nil().get_timestamp().is_none());
    }

    #[test]
    fn the_simple_form_is_the_hex_of_the_bytes() {
        let uuid = Uuid::parse_str("a1a2a3a4-b1b2-c1c2-d1d2-d3d4d5d6d7d8").unwrap();
        assert_eq!(
            uuid.simple().to_string(),
            "a1a2a3a4b1b2c1c2d1d2d3d4d5d6d7d8"
        );
        assert_eq!(uuid.as_bytes().len(), 16);
    }
}
