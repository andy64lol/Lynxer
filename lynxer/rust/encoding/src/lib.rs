//! Lynxer `encoding` stdlib backend: binary/text codecs on the usual crates.
//!
//! The encoded form of a payload is always text, and the raw form is always a
//! `bytes` value: an encode op takes `bytes` and returns a `str`, a decode op
//! takes a `str` and returns `bytes`. A payload that is not valid UTF-8 is
//! therefore representable (a decode used to answer `""`).
//!
//! Failures are in-band with the scalar-sentinel family documented in
//! `docs/stdlib-contracts.md`: a decode yields an empty `bytes` and a `*Valid`
//! predicate yields `false`, so a program tests the result rather than
//! expecting an exception. `*Valid` reports whether the input is well formed in
//! that codec.
//!
//! Decoding is strict apart from two documented conveniences: base64 and base32
//! padding is optional (encode always emits it) and base32 accepts either case.
//! Whitespace is never accepted, and a malformed `%` escape is a failure rather
//! than a passthrough.

use base64::alphabet;
use base64::engine::general_purpose::GeneralPurpose;
use base64::engine::{DecodePaddingMode, Engine, GeneralPurposeConfig};
use lynxer_abi::{export_bytes_buffers, export_int, export_string_buffers, lynxer_module};
use percent_encoding::{percent_decode_str, percent_encode, AsciiSet, NON_ALPHANUMERIC};

/// base64, standard alphabet, padded on encode and padding-optional on decode.
const BASE64_STANDARD: GeneralPurpose = GeneralPurpose::new(
    &alphabet::STANDARD,
    GeneralPurposeConfig::new().with_decode_padding_mode(DecodePaddingMode::Indifferent),
);

/// base64, URL-safe alphabet, same padding rule.
const BASE64_URL: GeneralPurpose = GeneralPurpose::new(
    &alphabet::URL_SAFE,
    GeneralPurposeConfig::new().with_decode_padding_mode(DecodePaddingMode::Indifferent),
);

/// The set `encodeURIComponent` leaves unescaped: `A-Z a-z 0-9 - _ . ! ~ * ' ( )`.
const COMPONENT: &AsciiSet = &NON_ALPHANUMERIC
    .remove(b'-')
    .remove(b'_')
    .remove(b'.')
    .remove(b'!')
    .remove(b'~')
    .remove(b'*')
    .remove(b'\'')
    .remove(b'(')
    .remove(b')');

/// base32 decoding that accepts upper- or lower-case and optional padding.
///
/// `data_encoding::BASE32` decodes upper case only, so the input is folded
/// first; RFC 4648 lets a decoder accept either case.
fn base32_decode(text: &str) -> Option<Vec<u8>> {
    let folded = text.to_ascii_uppercase();
    data_encoding::BASE32
        .decode(folded.as_bytes())
        .or_else(|_| data_encoding::BASE32_NOPAD.decode(folded.as_bytes()))
        .ok()
}

/// True when every `%` in `text` is followed by two hexadecimal digits.
///
/// `percent_decode_str` passes a malformed escape through untouched, so decoding
/// alone cannot tell well-formed input from sloppy input: this is the check
/// `percentValid` reports and `percentDecode` requires.
fn percent_escapes_well_formed(text: &str) -> bool {
    let bytes = text.as_bytes();
    let mut index = 0usize;
    while index < bytes.len() {
        if bytes[index] == b'%' {
            if index + 2 >= bytes.len()
                || !bytes[index + 1].is_ascii_hexdigit()
                || !bytes[index + 2].is_ascii_hexdigit()
            {
                return false;
            }
            index += 3;
            continue;
        }
        index += 1;
    }
    true
}

/// Rewrites the `z` shorthand (four zero bytes) as the `!!!!!` it abbreviates.
///
/// `ascii85` 0.2.1 decodes `z` and then range-checks it as a digit, so it
/// rejects every `z` it should accept. A `z` is only legal on a five-digit
/// group boundary, so expanding it there is exactly equivalent; a `z` anywhere
/// else is left alone for the crate to reject as the malformed input it is.
fn ascii85_payload(input: &str) -> String {
    let body = input.trim().trim_start_matches("<~").trim_end_matches("~>");
    let mut expanded = String::with_capacity(body.len());
    let mut digits_in_group = 0usize;
    for character in body.chars() {
        if character.is_ascii_whitespace() {
            expanded.push(character);
            continue;
        }
        let code = character as u32;
        if character == 'z' && digits_in_group == 0 {
            expanded.push_str("!!!!!");
            continue;
        }
        expanded.push(character);
        // Count only the digits the codec itself counts, so an invalid
        // character cannot shift the group boundary.
        if (33..=117).contains(&code) {
            digits_in_group = (digits_in_group + 1) % 5;
        }
    }
    expanded
}

// --- base64 ------------------------------------------------------------------

export_string_buffers!(encoding_base64_encode, args, {
    BASE64_STANDARD.encode(args.bytes(0))
});

export_bytes_buffers!(encoding_base64_decode, args, {
    BASE64_STANDARD.decode(args.string(0)).unwrap_or_default()
});

export_int!(encoding_base64_valid, args, {
    BASE64_STANDARD.decode(args.string(0)).is_ok() as i64
});

export_string_buffers!(encoding_base64_url_encode, args, {
    BASE64_URL.encode(args.bytes(0))
});

export_bytes_buffers!(encoding_base64_url_decode, args, {
    BASE64_URL.decode(args.string(0)).unwrap_or_default()
});

export_int!(encoding_base64_url_valid, args, {
    BASE64_URL.decode(args.string(0)).is_ok() as i64
});

// --- hex ---------------------------------------------------------------------

export_string_buffers!(encoding_hex_encode, args, { hex::encode(args.bytes(0)) });

export_string_buffers!(encoding_hex_encode_upper, args, {
    hex::encode(args.bytes(0)).to_ascii_uppercase()
});

export_bytes_buffers!(encoding_hex_decode, args, {
    hex::decode(args.string(0)).unwrap_or_default()
});

export_int!(encoding_hex_valid, args, {
    hex::decode(args.string(0)).is_ok() as i64
});

// --- base32 ------------------------------------------------------------------

export_string_buffers!(encoding_base32_encode, args, {
    data_encoding::BASE32.encode(args.bytes(0))
});

export_bytes_buffers!(encoding_base32_decode, args, {
    base32_decode(args.string(0)).unwrap_or_default()
});

export_int!(encoding_base32_valid, args, {
    base32_decode(args.string(0)).is_some() as i64
});

// --- base58 ------------------------------------------------------------------

export_string_buffers!(encoding_base58_encode, args, {
    bs58::encode(args.bytes(0)).into_string()
});

export_bytes_buffers!(encoding_base58_decode, args, {
    bs58::decode(args.string(0)).into_vec().unwrap_or_default()
});

export_int!(encoding_base58_valid, args, {
    bs58::decode(args.string(0)).into_vec().is_ok() as i64
});

// --- ascii85 -----------------------------------------------------------------

export_string_buffers!(encoding_ascii85_encode, args, {
    ascii85::encode(args.bytes(0))
});

export_bytes_buffers!(encoding_ascii85_decode, args, {
    ascii85::decode(&ascii85_payload(args.string(0))).unwrap_or_default()
});

export_int!(encoding_ascii85_valid, args, {
    ascii85::decode(&ascii85_payload(args.string(0))).is_ok() as i64
});

// --- percent encoding --------------------------------------------------------

export_string_buffers!(encoding_percent_encode, args, {
    percent_encode(args.bytes(0), COMPONENT).to_string()
});

export_bytes_buffers!(encoding_percent_decode, args, {
    let text = args.string(0);
    if !percent_escapes_well_formed(text) {
        return Vec::new();
    }
    percent_decode_str(text).collect()
});

export_int!(encoding_percent_valid, args, {
    percent_escapes_well_formed(args.string(0)) as i64
});

// --- quoted-printable --------------------------------------------------------

export_string_buffers!(encoding_quoted_printable_encode, args, {
    String::from_utf8(quoted_printable::encode(args.bytes(0))).unwrap_or_default()
});

export_bytes_buffers!(encoding_quoted_printable_decode, args, {
    quoted_printable::decode(
        args.string(0).as_bytes(),
        quoted_printable::ParseMode::Strict,
    )
    .unwrap_or_default()
});

export_int!(encoding_quoted_printable_valid, args, {
    quoted_printable::decode(
        args.string(0).as_bytes(),
        quoted_printable::ParseMode::Strict,
    )
    .is_ok() as i64
});

const OPS: &[(&str, &str, &str)] = &[
    (
        "base64Encode",
        "encoding_base64_encode",
        "cdecl:cstring(...,bytes)",
    ),
    (
        "base64Decode",
        "encoding_base64_decode",
        "cdecl:bytes(...,bytes)",
    ),
    ("base64Valid", "encoding_base64_valid", "cdecl:int64(...)"),
    (
        "base64UrlEncode",
        "encoding_base64_url_encode",
        "cdecl:cstring(...,bytes)",
    ),
    (
        "base64UrlDecode",
        "encoding_base64_url_decode",
        "cdecl:bytes(...,bytes)",
    ),
    (
        "base64UrlValid",
        "encoding_base64_url_valid",
        "cdecl:int64(...)",
    ),
    ("hexEncode", "encoding_hex_encode", "cdecl:cstring(...,bytes)"),
    (
        "hexEncodeUpper",
        "encoding_hex_encode_upper",
        "cdecl:cstring(...,bytes)",
    ),
    ("hexDecode", "encoding_hex_decode", "cdecl:bytes(...,bytes)"),
    ("hexValid", "encoding_hex_valid", "cdecl:int64(...)"),
    (
        "base32Encode",
        "encoding_base32_encode",
        "cdecl:cstring(...,bytes)",
    ),
    (
        "base32Decode",
        "encoding_base32_decode",
        "cdecl:bytes(...,bytes)",
    ),
    ("base32Valid", "encoding_base32_valid", "cdecl:int64(...)"),
    (
        "base58Encode",
        "encoding_base58_encode",
        "cdecl:cstring(...,bytes)",
    ),
    (
        "base58Decode",
        "encoding_base58_decode",
        "cdecl:bytes(...,bytes)",
    ),
    ("base58Valid", "encoding_base58_valid", "cdecl:int64(...)"),
    (
        "ascii85Encode",
        "encoding_ascii85_encode",
        "cdecl:cstring(...,bytes)",
    ),
    (
        "ascii85Decode",
        "encoding_ascii85_decode",
        "cdecl:bytes(...,bytes)",
    ),
    ("ascii85Valid", "encoding_ascii85_valid", "cdecl:int64(...)"),
    (
        "percentEncode",
        "encoding_percent_encode",
        "cdecl:cstring(...,bytes)",
    ),
    (
        "percentDecode",
        "encoding_percent_decode",
        "cdecl:bytes(...,bytes)",
    ),
    ("percentValid", "encoding_percent_valid", "cdecl:int64(...)"),
    (
        "quotedPrintableEncode",
        "encoding_quoted_printable_encode",
        "cdecl:cstring(...,bytes)",
    ),
    (
        "quotedPrintableDecode",
        "encoding_quoted_printable_decode",
        "cdecl:bytes(...,bytes)",
    ),
    (
        "quotedPrintableValid",
        "encoding_quoted_printable_valid",
        "cdecl:int64(...)",
    ),
];

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::*;
    use percent_encoding::utf8_percent_encode;

    /// A decoded payload as text, or `""` when it is not valid UTF-8.
    fn text_of(bytes: Vec<u8>) -> String {
        String::from_utf8(bytes).unwrap_or_default()
    }

    #[test]
    fn base64_round_trips_and_accepts_unpadded_input() {
        assert_eq!(BASE64_STANDARD.encode(b"Lynxer"), "THlueGVy");
        assert_eq!(BASE64_STANDARD.encode(b"Lynx"), "THlueA==");
        let decoded = |text: &str| {
            BASE64_STANDARD
                .decode(text)
                .map(text_of)
                .unwrap_or_default()
        };
        assert_eq!(decoded("THlueGVy"), "Lynxer");
        assert_eq!(decoded("THlueA"), "Lynx"); // padding is optional
        assert_eq!(decoded("THlueA="), "Lynx");
        assert_eq!(decoded("THlueA=="), "Lynx");
        assert_eq!(decoded("THlueGVy="), ""); // padding after a full group
        assert_eq!(decoded("THlueA==="), ""); // too much padding
        assert_eq!(decoded("THlueGVy\n"), ""); // whitespace is not accepted
    }

    #[test]
    fn base64_url_uses_the_url_safe_alphabet() {
        // 0xFB 0xFF is `+/8=` in the standard alphabet and `-_8=` in URL-safe.
        assert_eq!(BASE64_STANDARD.encode([0xFB, 0xFF]), "+/8=");
        assert_eq!(BASE64_URL.encode([0xFB, 0xFF]), "-_8=");
        assert_eq!(BASE64_URL.decode("-_8").unwrap(), [0xFB, 0xFF]);
        // Those bytes are not valid UTF-8, so the text view is the sentinel.
        assert_eq!(text_of(vec![0xFB, 0xFF]), "");
    }

    #[test]
    fn hex_accepts_both_cases_and_rejects_malformed_input() {
        assert_eq!(hex::encode("Lynxer"), "4c796e786572");
        assert_eq!(hex::decode("4C796E786572").unwrap(), b"Lynxer");
        assert!(hex::decode("4c796e78657").is_err());
        assert!(hex::decode("zz").is_err());
    }

    #[test]
    fn base32_accepts_either_case_with_optional_padding() {
        let encoded = data_encoding::BASE32.encode(b"Lynxer");
        assert_eq!(base32_decode(&encoded).unwrap(), b"Lynxer");
        assert_eq!(
            base32_decode(encoded.trim_end_matches('=')).unwrap(),
            b"Lynxer"
        );
        assert_eq!(
            base32_decode(&encoded.to_ascii_lowercase()).unwrap(),
            b"Lynxer"
        );
        // On its own the crate rejects lower case.
        assert!(data_encoding::BASE32
            .decode(encoded.to_ascii_lowercase().as_bytes())
            .is_err());
        assert!(base32_decode("1").is_none());
    }

    #[test]
    fn base58_round_trips() {
        let encoded = bs58::encode(b"Lynxer").into_string();
        assert_eq!(bs58::decode(&encoded).into_vec().unwrap(), b"Lynxer");
        // 0, O, I and l are not in the base58 alphabet.
        assert!(bs58::decode("0OIl").into_vec().is_err());
    }

    #[test]
    fn ascii85_round_trips_including_the_zero_shorthand() {
        let encoded = ascii85::encode(b"Lynxer");
        assert_eq!(
            ascii85::decode(&ascii85_payload(&encoded)).unwrap(),
            b"Lynxer"
        );
        // Four zero bytes are `z` in the shorthand; the crate alone rejects it.
        assert!(ascii85::decode("z").is_err());
        assert_eq!(
            ascii85::decode(&ascii85_payload("z")).unwrap(),
            [0, 0, 0, 0]
        );
        assert_eq!(
            ascii85::decode(&ascii85_payload("<~z!!!!!~>")).unwrap(),
            [0, 0, 0, 0, 0, 0, 0, 0]
        );
        // A `z` that is not on a group boundary stays invalid.
        assert!(ascii85::decode(&ascii85_payload("!z")).is_err());
    }

    #[test]
    fn percent_encoding_matches_encode_uri_component() {
        let encoded = utf8_percent_encode("a b/c?d=e&f", COMPONENT).to_string();
        assert_eq!(encoded, "a%20b%2Fc%3Fd%3De%26f");
        assert_eq!(
            percent_decode_str(&encoded).decode_utf8().unwrap(),
            "a b/c?d=e&f"
        );
        // Unreserved characters survive, including the JS extras.
        assert_eq!(
            utf8_percent_encode("-_.!~*'()", COMPONENT).to_string(),
            "-_.!~*'()"
        );
        // The decoder passes a malformed escape through untouched, so the
        // escape scan is what makes one detectable.
        assert!(percent_escapes_well_formed("a%20b"));
        assert!(percent_escapes_well_formed("%C3%A9"));
        assert!(!percent_escapes_well_formed("%2"));
        assert!(!percent_escapes_well_formed("%zz"));
        assert!(!percent_escapes_well_formed("abc%"));
        assert!(!percent_escapes_well_formed("100% sure"));
        assert_eq!(percent_decode_str("%2").decode_utf8().unwrap(), "%2");
    }

    #[test]
    fn quoted_printable_round_trips() {
        let encoded = quoted_printable::encode_to_str("caf\u{E9}");
        assert_eq!(encoded, "caf=C3=A9");
        assert_eq!(
            quoted_printable::decode(encoded.as_bytes(), quoted_printable::ParseMode::Robust)
                .unwrap(),
            "caf\u{E9}".as_bytes()
        );
        // A non-hex escape is malformed in Strict mode; Robust passes it through.
        assert!(quoted_printable::decode("caf=ZZ", quoted_printable::ParseMode::Strict).is_err());
        assert!(quoted_printable::decode("caf=ZZ", quoted_printable::ParseMode::Robust).is_ok());
    }
}

