//! Lynxer `re` stdlib backend: Python-flavoured regular expressions.
//!
//! The engine is [`lynxer_regex_engine`], a Rust `fancy-regex` build that
//! implements a PCRE-compatible superset of ECMAScript: lookahead and
//! lookbehind (including variable-length lookbehind), atomic groups,
//! possessive quantifiers, backreferences and `\p{…}` property escapes all
//! work, and `(?x)` verbose mode is honoured. Patterns are never rewritten.
//!
//! Failures are in-band, as `docs/stdlib-contracts.md` requires: an invalid
//! pattern makes a predicate `false`, a string result `""`, an index `-1`, and
//! a list `[]`.

use lynxer_abi::{export_int, export_string, lynxer_module};
use lynxer_regex_engine as engine;
use lynxer_regex_engine::{Json, Pattern};

/// Compiles a pattern, running `body` only when it is valid.
macro_rules! with_pattern {
    ($pattern:expr, $flags:expr, $fallback:expr, |$parsed:ident| $body:block) => {
        match Pattern::compile($pattern, $flags) {
            Some($parsed) => $body,
            None => $fallback,
        }
    };
}

// --- matching ---------------------------------------------------------------

export_int!(re_test, args, {
    with_pattern!(args.string(0), 0, 0, |pattern| {
        pattern.is_match(args.string(1)) as i64
    })
});

export_string!(re_match, args, {
    with_pattern!(args.string(0), 0, String::new(), |pattern| {
        engine::find_at_start(&pattern, args.string(1)).unwrap_or_default()
    })
});

export_string!(re_match_full, args, {
    with_pattern!(args.string(0), 0, String::new(), |pattern| {
        engine::find_full(&pattern, args.string(1)).unwrap_or_default()
    })
});

export_string!(re_search, args, {
    with_pattern!(args.string(0), 0, String::new(), |pattern| {
        engine::find_first(&pattern, args.string(1)).unwrap_or_default()
    })
});

export_string!(re_findall, args, {
    with_pattern!(args.string(0), 0, "[]".to_string(), |pattern| {
        engine::find_all_json(&pattern, args.string(1))
    })
});

export_int!(re_count, args, {
    with_pattern!(args.string(0), 0, 0, |pattern| {
        engine::count(&pattern, args.string(1))
    })
});

export_string!(re_groups, args, {
    with_pattern!(args.string(0), 0, "[]".to_string(), |pattern| {
        engine::groups_json(&pattern, args.string(1))
    })
});

export_string!(re_groups_all, args, {
    with_pattern!(args.string(0), 0, "[]".to_string(), |pattern| {
        engine::groups_all_json(&pattern, args.string(1))
    })
});

export_string!(re_named, args, {
    with_pattern!(args.string(0), 0, "{}".to_string(), |pattern| {
        engine::named_json(&pattern, args.string(1))
    })
});

// --- replacement ------------------------------------------------------------

export_string!(re_sub, args, {
    with_pattern!(args.string(0), 0, String::new(), |pattern| {
        engine::replace(&pattern, args.string(1), args.string(2), -1).0
    })
});

export_string!(re_sub_n, args, {
    with_pattern!(args.string(0), 0, String::new(), |pattern| {
        engine::replace(&pattern, args.string(1), args.string(2), args.int(0)).0
    })
});

export_string!(re_subn, args, {
    match Pattern::compile(args.string(0), 0) {
        Some(pattern) => {
            let (result, count) = engine::replace(&pattern, args.string(1), args.string(2), -1);
            engine::dump(&Json::Object(vec![
                ("result".to_string(), Json::String(result)),
                ("count".to_string(), Json::Integer(count)),
            ]))
        }
        None => "{\"result\": \"\", \"count\": 0}".to_string(),
    }
});

// --- splitting and inspection ----------------------------------------------

export_string!(re_split, args, {
    with_pattern!(args.string(0), 0, "[]".to_string(), |pattern| {
        engine::split_json(&pattern, args.string(1), -1)
    })
});

export_string!(re_split_n, args, {
    with_pattern!(args.string(0), 0, "[]".to_string(), |pattern| {
        engine::split_json(&pattern, args.string(1), args.int(0))
    })
});

export_string!(re_escape, args, { engine::escape(args.string(0)) });

export_int!(re_match_start, args, {
    with_pattern!(args.string(0), 0, -1, |pattern| {
        engine::first_match_pos(&pattern, args.string(1))
    })
});

export_int!(re_match_end, args, {
    with_pattern!(args.string(0), 0, -1, |pattern| {
        engine::match_end_pos(&pattern, args.string(1))
    })
});

export_string!(re_find_spans, args, {
    with_pattern!(args.string(0), 0, "[]".to_string(), |pattern| {
        engine::spans_json(&pattern, args.string(1))
    })
});

// --- flag variants ----------------------------------------------------------

export_int!(re_test_ignore_case, args, {
    with_pattern!(args.string(0), engine::IGNORE_CASE, 0, |pattern| {
        pattern.is_match(args.string(1)) as i64
    })
});

export_string!(re_match_ignore_case, args, {
    with_pattern!(
        args.string(0),
        engine::IGNORE_CASE,
        String::new(),
        |pattern| { engine::find_at_start(&pattern, args.string(1)).unwrap_or_default() }
    )
});

export_string!(re_search_ignore_case, args, {
    with_pattern!(
        args.string(0),
        engine::IGNORE_CASE,
        String::new(),
        |pattern| { engine::find_first(&pattern, args.string(1)).unwrap_or_default() }
    )
});

export_string!(re_findall_ignore_case, args, {
    with_pattern!(
        args.string(0),
        engine::IGNORE_CASE,
        "[]".to_string(),
        |pattern| { engine::find_all_json(&pattern, args.string(1)) }
    )
});

export_string!(re_sub_ignore_case, args, {
    with_pattern!(
        args.string(0),
        engine::IGNORE_CASE,
        String::new(),
        |pattern| { engine::replace(&pattern, args.string(1), args.string(2), -1).0 }
    )
});

export_string!(re_findall_multiline, args, {
    with_pattern!(
        args.string(0),
        engine::MULTILINE,
        "[]".to_string(),
        |pattern| { engine::find_all_json(&pattern, args.string(1)) }
    )
});

export_string!(re_sub_multiline, args, {
    with_pattern!(
        args.string(0),
        engine::MULTILINE,
        String::new(),
        |pattern| { engine::replace(&pattern, args.string(1), args.string(2), -1).0 }
    )
});

export_string!(re_search_dotall, args, {
    with_pattern!(args.string(0), engine::DOTALL, String::new(), |pattern| {
        engine::find_first(&pattern, args.string(1)).unwrap_or_default()
    })
});

const OPS: &[(&str, &str, &str)] = &[
    ("test", "re_test", "cdecl:int64(...)"),
    ("match", "re_match", "cdecl:cstring(...)"),
    ("matchFull", "re_match_full", "cdecl:cstring(...)"),
    ("search", "re_search", "cdecl:cstring(...)"),
    ("findall", "re_findall", "cdecl:cstring(...)"),
    ("count", "re_count", "cdecl:int64(...)"),
    ("groups", "re_groups", "cdecl:cstring(...)"),
    ("groupsAll", "re_groups_all", "cdecl:cstring(...)"),
    ("named", "re_named", "cdecl:cstring(...)"),
    ("sub", "re_sub", "cdecl:cstring(...)"),
    ("subN", "re_sub_n", "cdecl:cstring(...)"),
    ("subn", "re_subn", "cdecl:cstring(...)"),
    ("split", "re_split", "cdecl:cstring(...)"),
    ("splitN", "re_split_n", "cdecl:cstring(...)"),
    ("escape", "re_escape", "cdecl:cstring(...)"),
    ("matchStart", "re_match_start", "cdecl:int64(...)"),
    ("matchEnd", "re_match_end", "cdecl:int64(...)"),
    ("findSpans", "re_find_spans", "cdecl:cstring(...)"),
    ("testIgnoreCase", "re_test_ignore_case", "cdecl:int64(...)"),
    (
        "matchIgnoreCase",
        "re_match_ignore_case",
        "cdecl:cstring(...)",
    ),
    (
        "searchIgnoreCase",
        "re_search_ignore_case",
        "cdecl:cstring(...)",
    ),
    (
        "findallIgnoreCase",
        "re_findall_ignore_case",
        "cdecl:cstring(...)",
    ),
    ("subIgnoreCase", "re_sub_ignore_case", "cdecl:cstring(...)"),
    (
        "findallMultiline",
        "re_findall_multiline",
        "cdecl:cstring(...)",
    ),
    ("subMultiline", "re_sub_multiline", "cdecl:cstring(...)"),
    ("searchDotall", "re_search_dotall", "cdecl:cstring(...)"),
];

lynxer_module!(OPS);
