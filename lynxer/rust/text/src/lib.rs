//! Unicode-aware case conversion and character predicates for `text`.

use lynxer_abi::{export_int, export_string, lynxer_module};

fn all_alpha(value: &str) -> bool {
    !value.is_empty() && value.chars().all(char::is_alphabetic)
}

fn all_numeric(value: &str) -> bool {
    !value.is_empty() && value.chars().all(char::is_numeric)
}

export_string!(text_upper, args, {
    args.string(0)
        .chars()
        .flat_map(char::to_uppercase)
        .collect()
});

export_string!(text_lower, args, {
    args.string(0)
        .chars()
        .flat_map(char::to_lowercase)
        .collect()
});

export_int!(text_is_alpha, args, { all_alpha(args.string(0)) as i64 });

export_int!(text_is_numeric, args, {
    all_numeric(args.string(0)) as i64
});

const OPS: &[(&str, &str, &str)] = &[
    ("upper", "text_upper", "cdecl:cstring(...)"),
    ("lower", "text_lower", "cdecl:cstring(...)"),
    ("isAlpha", "text_is_alpha", "cdecl:int64(...)"),
    ("isNumeric", "text_is_numeric", "cdecl:int64(...)"),
];

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::{all_alpha, all_numeric};

    #[test]
    fn case_conversion_handles_unicode_expansion_and_combining_marks() {
        assert_eq!(
            "Straße e\u{301}"
                .chars()
                .flat_map(char::to_uppercase)
                .collect::<String>(),
            "STRASSE E\u{301}"
        );
        assert_eq!(
            "İΣ"
                .chars()
                .flat_map(char::to_lowercase)
                .collect::<String>(),
            "i\u{307}σ"
        );
    }

    #[test]
    fn predicates_use_unicode_character_properties() {
        assert!(all_alpha("café"));
        assert!(all_numeric("１２٣"));
        assert!(!all_alpha("e\u{301}"));
        assert!(!all_alpha(""));
        assert!(!all_numeric(""));
    }
}
