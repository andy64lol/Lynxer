//! Lynxer `regex` stdlib backend: extended helpers with a named compiled-pattern
//! cache.
//!
//! Shares the [`lynxer_regex_engine`] (Rust `fancy-regex`) with the `re`
//! module, so `findLetters`/`findDigits` stay ASCII-only while
//! `findallOverlapping` is still emulated — but patterns now get the full
//! PCRE-compatible syntax (lookbehind, atomic groups, `\p{…}`, `(?x)`).
//!
//! The cache is keyed by name and kept in a process-wide map, so a compiled
//! pattern is reused across calls and `cacheKeys` reports the names sorted.

use std::collections::BTreeMap;
use std::sync::{Mutex, OnceLock};

use lynxer_abi::{export_int, export_string, lynxer_module};
use lynxer_regex_engine as engine;
use lynxer_regex_engine::{Json, Pattern};

fn cache() -> &'static Mutex<BTreeMap<String, Pattern>> {
    static CACHE: OnceLock<Mutex<BTreeMap<String, Pattern>>> = OnceLock::new();
    CACHE.get_or_init(|| Mutex::new(BTreeMap::new()))
}

fn cached(name: &str) -> Option<Pattern> {
    cache().lock().ok()?.get(name).cloned()
}

/// Compiles `pattern` with `flags` and runs `body`; invalid patterns sentinel.
macro_rules! with_pattern {
    ($pattern:expr, $fallback:expr, |$parsed:ident| $body:block) => {
        match Pattern::compile($pattern, 0) {
            Some($parsed) => $body,
            None => $fallback,
        }
    };
}

/// Runs `body` against a cached pattern, or returns `fallback`.
macro_rules! with_cached {
    ($name:expr, $fallback:expr, |$parsed:ident| $body:block) => {
        match cached($name) {
            Some($parsed) => $body,
            None => $fallback,
        }
    };
}

// --- the compiled-pattern cache --------------------------------------------

export_int!(regex_compile, args, {
    match Pattern::compile(args.string(1), engine::flags_from_text(args.string(2))) {
        Some(pattern) => match cache().lock() {
            Ok(mut entries) => {
                entries.insert(args.string(0).to_string(), pattern);
                1
            }
            Err(_) => 0,
        },
        None => 0,
    }
});

export_int!(regex_clear_cache, args, {
    let _ = args;
    match cache().lock() {
        Ok(mut entries) => entries.clear(),
        Err(_) => {}
    };
    0
});

export_string!(regex_cache_keys, args, {
    let _ = args;
    let keys: Vec<Json> = match cache().lock() {
        Ok(entries) => entries.keys().cloned().map(Json::String).collect(),
        Err(_) => Vec::new(),
    };
    engine::dump(&Json::Array(keys))
});

// --- cached ops -------------------------------------------------------------

export_int!(regex_test_compiled, args, {
    with_cached!(args.string(0), 0, |pattern| {
        pattern.is_match(args.string(1)) as i64
    })
});

export_string!(regex_match_compiled, args, {
    with_cached!(args.string(0), String::new(), |pattern| {
        engine::find_first(&pattern, args.string(1)).unwrap_or_default()
    })
});

export_string!(regex_findall_compiled, args, {
    with_cached!(args.string(0), "[]".to_string(), |pattern| {
        engine::find_all_json(&pattern, args.string(1))
    })
});

export_string!(regex_sub_compiled, args, {
    with_cached!(args.string(0), args.string(2).to_string(), |pattern| {
        engine::replace(&pattern, args.string(1), args.string(2), -1).0
    })
});

// --- helpers ----------------------------------------------------------------

export_int!(regex_is_valid, args, {
    Pattern::compile(args.string(0), 0).is_some() as i64
});

export_string!(regex_extract, args, {
    with_pattern!(args.string(0), "{}".to_string(), |pattern| {
        engine::named_json(&pattern, args.string(1))
    })
});

export_string!(regex_extract_all, args, {
    let pattern = args.string(0);
    let subject = args.string(1);
    match Pattern::compile(pattern, 0) {
        Some(parsed) => {
            let fields = parsed.names().to_vec();
            let rows: Vec<Json> = engine::matches(&parsed, subject)
                .into_iter()
                .map(|record| {
                    Json::Object(
                        fields
                            .iter()
                            .map(|(name, index)| {
                                let value = record
                                    .groups
                                    .get(*index - 1)
                                    .and_then(|group| group.clone())
                                    .map(Json::String)
                                    .unwrap_or(Json::Null);
                                (name.clone(), value)
                            })
                            .collect(),
                    )
                })
                .collect();
            engine::dump(&Json::Array(rows))
        }
        None => "[]".to_string(),
    }
});

export_string!(regex_unique, args, {
    with_pattern!(args.string(0), "[]".to_string(), |pattern| {
        engine::unique_json(&pattern, args.string(1))
    })
});

export_string!(regex_last_match, args, {
    with_pattern!(args.string(0), String::new(), |pattern| {
        engine::last_match(&pattern, args.string(1))
    })
});

export_string!(regex_findall_overlapping, args, {
    with_pattern!(args.string(0), "[]".to_string(), |pattern| {
        engine::overlapping_json(&pattern, args.string(1))
    })
});

/// Replaces matches manually so a literal replacement and a highlight wrapper
/// can share the loop; `nth` selects one match (`< 0` means all).
fn replace_manual(
    subject: &str,
    pattern: &Pattern,
    nth: i64,
    before: &str,
    replacement: &str,
    after: &str,
    highlight: bool,
) -> String {
    let mut output = String::new();
    let mut last = 0usize;
    for (position, record) in engine::matches(pattern, subject).iter().enumerate() {
        output.push_str(&subject[last..record.start]);
        if nth < 0 || (position as i64) + 1 == nth {
            output.push_str(before);
            output.push_str(if highlight { &record.text } else { replacement });
            output.push_str(after);
        } else {
            output.push_str(&record.text);
        }
        last = record.end;
    }
    output.push_str(&subject[last..]);
    output
}

export_string!(regex_replace_nth, args, {
    with_pattern!(args.string(0), args.string(2).to_string(), |pattern| {
        replace_manual(
            args.string(2),
            &pattern,
            args.int(0),
            "",
            args.string(1),
            "",
            false,
        )
    })
});

export_string!(regex_replace_all_literal, args, {
    with_pattern!(args.string(0), args.string(2).to_string(), |pattern| {
        replace_manual(args.string(2), &pattern, -1, "", args.string(1), "", false)
    })
});

export_string!(regex_highlight, args, {
    with_pattern!(args.string(0), args.string(3).to_string(), |pattern| {
        replace_manual(
            args.string(3),
            &pattern,
            -1,
            args.string(1),
            "",
            args.string(2),
            true,
        )
    })
});

export_string!(regex_split_keep, args, {
    // Wrapping the pattern in a capture group keeps the separators in the
    // result, which is what the wrapper's contract promises.
    let wrapped = format!("({})", args.string(0));
    with_pattern!(&wrapped, "[]".to_string(), |pattern| {
        engine::split_json(&pattern, args.string(1), -1)
    })
});

export_string!(regex_find_letters, args, {
    with_pattern!("[a-zA-Z]+", "[]".to_string(), |pattern| {
        engine::find_all_plain_json(&pattern, args.string(0))
    })
});

export_string!(regex_find_digits, args, {
    with_pattern!("\\d+", "[]".to_string(), |pattern| {
        engine::find_all_plain_json(&pattern, args.string(0))
    })
});

export_string!(regex_glob_to_regex, args, {
    engine::glob_to_regex(args.string(0))
});

export_int!(regex_count_matches, args, {
    with_pattern!(args.string(0), 0, |pattern| {
        engine::count(&pattern, args.string(1))
    })
});

export_int!(regex_first_match_pos, args, {
    with_pattern!(args.string(0), -1, |pattern| {
        engine::first_match_pos(&pattern, args.string(1))
    })
});

export_int!(regex_first_match_char_pos, args, {
    with_pattern!(args.string(0), -1, |pattern| {
        engine::first_match_char_pos(&pattern, args.string(1))
    })
});

export_string!(regex_truncate_match, args, {
    let subject = args.string(1);
    let max_len = args.int(0);
    match Pattern::compile(args.string(0), 0) {
        Some(pattern) => engine::truncate_match(&pattern, subject, max_len),
        None => engine::truncated_head(subject, max_len),
    }
});

const OPS: &[(&str, &str, &str)] = &[
    ("compile", "regex_compile", "cdecl:int64(...)"),
    ("testCompiled", "regex_test_compiled", "cdecl:int64(...)"),
    (
        "matchCompiled",
        "regex_match_compiled",
        "cdecl:cstring(...)",
    ),
    (
        "findallCompiled",
        "regex_findall_compiled",
        "cdecl:cstring(...)",
    ),
    ("subCompiled", "regex_sub_compiled", "cdecl:cstring(...)"),
    ("clearCache", "regex_clear_cache", "cdecl:int64(...)"),
    ("cacheKeys", "regex_cache_keys", "cdecl:cstring(...)"),
    ("isValid", "regex_is_valid", "cdecl:int64(...)"),
    ("extract", "regex_extract", "cdecl:cstring(...)"),
    ("extractAll", "regex_extract_all", "cdecl:cstring(...)"),
    ("unique", "regex_unique", "cdecl:cstring(...)"),
    ("lastMatch", "regex_last_match", "cdecl:cstring(...)"),
    (
        "findallOverlapping",
        "regex_findall_overlapping",
        "cdecl:cstring(...)",
    ),
    ("replaceNth", "regex_replace_nth", "cdecl:cstring(...)"),
    (
        "replaceAllLiteral",
        "regex_replace_all_literal",
        "cdecl:cstring(...)",
    ),
    ("highlight", "regex_highlight", "cdecl:cstring(...)"),
    ("splitKeep", "regex_split_keep", "cdecl:cstring(...)"),
    ("findLetters", "regex_find_letters", "cdecl:cstring(...)"),
    ("findDigits", "regex_find_digits", "cdecl:cstring(...)"),
    ("globToRegex", "regex_glob_to_regex", "cdecl:cstring(...)"),
    ("countMatches", "regex_count_matches", "cdecl:int64(...)"),
    ("firstMatchPos", "regex_first_match_pos", "cdecl:int64(...)"),
    (
        "firstMatchCharPos",
        "regex_first_match_char_pos",
        "cdecl:int64(...)",
    ),
    (
        "truncateMatch",
        "regex_truncate_match",
        "cdecl:cstring(...)",
    ),
];

lynxer_module!(OPS);
