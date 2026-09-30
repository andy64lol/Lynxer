//! Shared regular-expression engine for the Lynxer `re` and `regex` modules.
//!
//! Built on `fancy-regex`, which is a superset of the `regex` crate: in addition
//! to the ECMAScript-compatible core it supports lookaround (including
//! variable-length lookbehind), backreferences, atomic groups, possessive
//! quantifiers and Unicode property escapes such as `\p{Greek}`. That is the
//! whole point of this crate — the C++ backends it replaces could only offer
//! the ECMAScript subset of `std::regex`.
//!
//! Two behaviours are deliberately preserved from the previous engine, because
//! the modules document them and fixtures depend on them:
//!
//! * **Match iteration** follows Python/ECMAScript, not the `regex` crate: an
//!   empty match advances one *character*, and an empty match is still reported
//!   at the position where a previous non-empty match ended. The `regex`
//!   crate's own iterators suppress that last case, which changes both the
//!   count and the values of `findall`/`split` on patterns that can match the
//!   empty string.
//! * **In-band errors.** There is no error channel across the native ABI, so an
//!   invalid pattern (or a match-time backtracking failure) yields the scalar
//!   sentinel family from `docs/stdlib-contracts.md`.
//!
//! Offsets are byte offsets, as they were before; string handling is UTF-8
//! aware, so an empty match advances to the next character boundary rather than
//! the next byte.

use fancy_regex::{Captures, Regex, RegexBuilder};

/// Pattern flags, matching the letters `regex.compile` accepts.
pub const IGNORE_CASE: u32 = 1;
pub const MULTILINE: u32 = 2;
pub const DOTALL: u32 = 4;
pub const VERBOSE: u32 = 8;

/// Reads the `I`/`M`/`S`/`X` flag letters (either case) from a flag string.
pub fn flags_from_text(text: &str) -> u32 {
    let mut flags = 0;
    for character in text.chars() {
        match character {
            'I' | 'i' => flags |= IGNORE_CASE,
            'M' | 'm' => flags |= MULTILINE,
            'S' | 's' => flags |= DOTALL,
            'X' | 'x' => flags |= VERBOSE,
            _ => {}
        }
    }
    flags
}

/// A compiled pattern plus its named groups, in pattern order.
#[derive(Clone)]
pub struct Pattern {
    regex: Regex,
    /// `\A(?:…)\z`, used for a whole-string match. The absolute anchors are
    /// immune to an inline `(?m)`, which `^`/`$` are not.
    anchored: Regex,
    names: Vec<(String, usize)>,
}

impl Pattern {
    /// Compiles `raw`. `None` means the pattern is invalid.
    pub fn compile(raw: &str, flags: u32) -> Option<Pattern> {
        let build = |source: &str| -> Option<Regex> {
            RegexBuilder::new(source)
                .case_insensitive(flags & IGNORE_CASE != 0)
                .multi_line(flags & MULTILINE != 0)
                .dot_matches_new_line(flags & DOTALL != 0)
                .ignore_whitespace(flags & VERBOSE != 0)
                .build()
                .ok()
        };
        let regex = build(raw)?;
        let anchored = build(&format!(r"\A(?:{})\z", raw))?;
        let names = regex
            .capture_names()
            .enumerate()
            .filter_map(|(index, name)| name.map(|name| (name.to_string(), index)))
            .collect();
        Some(Pattern {
            regex,
            anchored,
            names,
        })
    }

    /// The number of capture groups, excluding the whole match.
    pub fn group_count(&self) -> usize {
        self.regex.captures_len().saturating_sub(1)
    }

    /// Named groups as `(name, capture index)`, in pattern order.
    pub fn names(&self) -> &[(String, usize)] {
        &self.names
    }

    pub fn is_match(&self, subject: &str) -> bool {
        self.regex.is_match(subject).unwrap_or(false)
    }

    /// True when the *whole* subject matches, like `std::regex_match`.
    pub fn is_full_match(&self, subject: &str) -> bool {
        self.anchored.is_match(subject).unwrap_or(false)
    }
}

/// One match and its capture groups (`None` for a group that did not
/// participate).
pub struct Record {
    pub start: usize,
    pub end: usize,
    pub text: String,
    pub groups: Vec<Option<String>>,
}

/// The next character boundary after `index`, or `subject.len()`.
fn advance(subject: &str, index: usize) -> usize {
    let mut next = index + 1;
    while next < subject.len() && !subject.is_char_boundary(next) {
        next += 1;
    }
    next.min(subject.len())
}

fn record(captures: &Captures<'_, str>, groups: usize) -> Record {
    let whole = captures.get(0).expect("group 0 always participates");
    let mut captured = Vec::with_capacity(groups);
    for index in 1..=groups {
        captured.push(captures.get(index).map(|m| m.as_str().to_string()));
    }
    Record {
        start: whole.start(),
        end: whole.end(),
        text: whole.as_str().to_string(),
        groups: captured,
    }
}

/// Every match, iterated the way Python and ECMAScript iterate: after any match
/// the search resumes at its end, and an empty match additionally consumes one
/// character so the scan always progresses. An empty match at the position a
/// previous match ended is reported (unlike the `regex` crate's iterators).
///
/// The search always runs over the whole subject via `captures_from_pos`, never
/// over a suffix: slicing would make `^`, `\A` and lookbehind see the slice
/// boundary as the start of the string.
pub fn matches(pattern: &Pattern, subject: &str) -> Vec<Record> {
    let groups = pattern.group_count();
    let mut records = Vec::new();
    let mut position = 0usize;
    loop {
        let captures = match pattern.regex.captures_from_pos(subject, position) {
            Ok(Some(captures)) => captures,
            _ => break,
        };
        let current = record(&captures, groups);
        let empty = current.start == current.end;
        let at_end = current.end >= subject.len();
        let end = current.end;
        records.push(current);
        if empty {
            if at_end {
                break;
            }
            position = advance(subject, end);
        } else {
            position = end;
        }
    }
    records
}

/// The first match anywhere, by text.
pub fn find_first(pattern: &Pattern, subject: &str) -> Option<String> {
    match pattern.regex.find(subject) {
        Ok(Some(found)) => Some(found.as_str().to_string()),
        _ => None,
    }
}

/// The first match, provided it starts at the beginning of the subject.
pub fn find_at_start(pattern: &Pattern, subject: &str) -> Option<String> {
    match pattern.regex.find(subject) {
        Ok(Some(found)) if found.start() == 0 => Some(found.as_str().to_string()),
        _ => None,
    }
}

/// The whole subject as a match, or `None`.
pub fn find_full(pattern: &Pattern, subject: &str) -> Option<String> {
    if pattern.is_full_match(subject) {
        Some(subject.to_string())
    } else {
        None
    }
}

// --- JSON ------------------------------------------------------------------
//
// A miniature writer matching `lynxer/stdlib/native_json.hpp` byte for byte so
// the modules' output is unchanged: insertion-ordered objects, `", "` between
// items, and the same escape table.

pub enum Json {
    Null,
    Integer(i64),
    String(String),
    Array(Vec<Json>),
    Object(Vec<(String, Json)>),
}

fn escape_into(value: &str, output: &mut Vec<u8>) {
    output.push(b'"');
    for &byte in value.as_bytes() {
        match byte {
            b'"' => output.extend_from_slice(b"\\\""),
            b'\\' => output.extend_from_slice(b"\\\\"),
            0x08 => output.extend_from_slice(b"\\b"),
            0x0C => output.extend_from_slice(b"\\f"),
            b'\n' => output.extend_from_slice(b"\\n"),
            b'\r' => output.extend_from_slice(b"\\r"),
            b'\t' => output.extend_from_slice(b"\\t"),
            byte if byte < 0x20 => {
                output.extend_from_slice(format!("\\u{byte:04x}").as_bytes());
            }
            byte => output.push(byte),
        }
    }
    output.push(b'"');
}

fn dump_into(value: &Json, output: &mut Vec<u8>) {
    match value {
        Json::Null => output.extend_from_slice(b"null"),
        Json::Integer(number) => output.extend_from_slice(number.to_string().as_bytes()),
        Json::String(text) => escape_into(text, output),
        Json::Array(items) => {
            output.push(b'[');
            for (index, item) in items.iter().enumerate() {
                if index > 0 {
                    output.extend_from_slice(b", ");
                }
                dump_into(item, output);
            }
            output.push(b']');
        }
        Json::Object(fields) => {
            output.push(b'{');
            for (index, (key, item)) in fields.iter().enumerate() {
                if index > 0 {
                    output.extend_from_slice(b", ");
                }
                escape_into(key, output);
                output.extend_from_slice(b": ");
                dump_into(item, output);
            }
            output.push(b'}');
        }
    }
}

/// Serializes a value the way `native_json::dump(value, false)` does.
pub fn dump(value: &Json) -> String {
    let mut output = Vec::new();
    dump_into(value, &mut output);
    // Only ASCII was added around the (valid UTF-8) string contents.
    String::from_utf8(output).unwrap_or_default()
}

fn strings(items: impl IntoIterator<Item = String>) -> Json {
    Json::Array(items.into_iter().map(Json::String).collect())
}

fn group_text(captures: &Captures<'_, str>, index: usize) -> String {
    captures
        .get(index)
        .map(|m| m.as_str().to_string())
        .unwrap_or_default()
}

// --- Operations ------------------------------------------------------------

/// `findall`: the whole match when there are no groups, the single group when
/// there is one, and the group list otherwise.
pub fn find_all_json(pattern: &Pattern, subject: &str) -> String {
    let groups = pattern.group_count();
    let items: Vec<Json> = matches(pattern, subject)
        .into_iter()
        .map(|record| {
            if groups == 0 {
                Json::String(record.text)
            } else if groups == 1 {
                Json::String(record.groups[0].clone().unwrap_or_default())
            } else {
                strings(
                    record
                        .groups
                        .into_iter()
                        .map(|group| group.unwrap_or_default()),
                )
            }
        })
        .collect();
    dump(&Json::Array(items))
}

/// Every match as plain text, ignoring capture groups.
pub fn find_all_plain_json(pattern: &Pattern, subject: &str) -> String {
    dump(&strings(
        matches(pattern, subject).into_iter().map(|r| r.text),
    ))
}

pub fn count(pattern: &Pattern, subject: &str) -> i64 {
    matches(pattern, subject).len() as i64
}

/// Capture groups of the first match, as a JSON array.
pub fn groups_json(pattern: &Pattern, subject: &str) -> String {
    match pattern.regex.captures(subject) {
        Ok(Some(captures)) => {
            let groups = pattern.group_count();
            dump(&strings((1..=groups).map(|i| group_text(&captures, i))))
        }
        _ => dump(&Json::Array(Vec::new())),
    }
}

/// Capture groups of every match, as a JSON array of arrays.
pub fn groups_all_json(pattern: &Pattern, subject: &str) -> String {
    let rows: Vec<Json> = matches(pattern, subject)
        .into_iter()
        .map(|record| strings(record.groups.into_iter().map(|g| g.unwrap_or_default())))
        .collect();
    dump(&Json::Array(rows))
}

/// Named groups of the first match, as a JSON object; a group that did not
/// participate is `null`.
pub fn named_json(pattern: &Pattern, subject: &str) -> String {
    let captures = match pattern.regex.captures(subject) {
        Ok(Some(captures)) => captures,
        _ => return dump(&Json::Object(Vec::new())),
    };
    let fields = pattern
        .names()
        .iter()
        .map(|(name, index)| {
            let value = if captures.get(*index).is_some() {
                Json::String(group_text(&captures, *index))
            } else {
                Json::Null
            };
            (name.clone(), value)
        })
        .collect();
    dump(&Json::Object(fields))
}

/// `{start, end, match}` for every match.
pub fn spans_json(pattern: &Pattern, subject: &str) -> String {
    let items: Vec<Json> = matches(pattern, subject)
        .into_iter()
        .map(|record| {
            Json::Object(vec![
                ("start".to_string(), Json::Integer(record.start as i64)),
                ("end".to_string(), Json::Integer(record.end as i64)),
                ("match".to_string(), Json::String(record.text)),
            ])
        })
        .collect();
    dump(&Json::Array(items))
}

/// Distinct match texts, in order of first appearance.
pub fn unique_json(pattern: &Pattern, subject: &str) -> String {
    let mut seen: Vec<String> = Vec::new();
    for record in matches(pattern, subject) {
        if !seen.contains(&record.text) {
            seen.push(record.text);
        }
    }
    dump(&strings(seen))
}

/// The last match, by text.
pub fn last_match(pattern: &Pattern, subject: &str) -> String {
    matches(pattern, subject)
        .pop()
        .map(|record| record.text)
        .unwrap_or_default()
}

/// Every match found by restarting one character after the previous *start*.
pub fn overlapping_json(pattern: &Pattern, subject: &str) -> String {
    let mut items: Vec<String> = Vec::new();
    let mut position = 0usize;
    while position <= subject.len() {
        let found = match pattern.regex.find_from_pos(subject, position) {
            Ok(Some(found)) => found,
            _ => break,
        };
        let start = found.start();
        items.push(found.as_str().to_string());
        if start >= subject.len() {
            break;
        }
        position = advance(subject, start);
    }
    dump(&strings(items))
}

/// Expands a Python-style replacement against one match: `\1`, `\g<name>`,
/// `\g<0>`, the `\n`/`\t`/`\r`/`\\` escapes, and a literal `$`.
///
/// An undefined group *name* is left as the literal `$name`, and an undefined
/// group *number* expands to nothing — both preserved from the ECMAScript
/// format translation the C++ engine used.
fn expand(replacement: &str, captures: &Captures<'_, str>, names: &[(String, usize)]) -> String {
    let mut output = String::new();
    let bytes = replacement.as_bytes();
    let mut index = 0usize;
    while index < replacement.len() {
        let character = replacement[index..]
            .chars()
            .next()
            .expect("index is a char boundary");
        if character == '$' {
            output.push('$');
            index += 1;
            continue;
        }
        if character != '\\' {
            output.push(character);
            index += character.len_utf8();
            continue;
        }
        if index + 1 >= replacement.len() {
            output.push('\\');
            index += 1;
            continue;
        }
        let next = replacement[index + 1..]
            .chars()
            .next()
            .expect("index+1 is a char boundary");
        if next.is_ascii_digit() {
            let mut end = index + 1;
            while end < replacement.len() && bytes[end].is_ascii_digit() {
                end += 1;
            }
            let target = replacement[index + 1..end]
                .parse::<usize>()
                .unwrap_or(usize::MAX);
            output.push_str(&group_text(captures, target));
            index = end;
            continue;
        }
        if next == 'g' && index + 2 < replacement.len() && replacement[index + 2..].starts_with('<')
        {
            if let Some(offset) = replacement[index + 3..].find('>') {
                let name = &replacement[index + 3..index + 3 + offset];
                if let Ok(target) = name.parse::<usize>() {
                    output.push_str(&group_text(captures, target));
                } else if let Some((_, target)) = names.iter().find(|(key, _)| key == name) {
                    output.push_str(&group_text(captures, *target));
                } else {
                    output.push('$');
                    output.push_str(name);
                }
                index = index + 3 + offset + 1;
                continue;
            }
        }
        match next {
            'n' => output.push('\n'),
            't' => output.push('\t'),
            'r' => output.push('\r'),
            '\\' => output.push('\\'),
            other => output.push(other),
        }
        index += 1 + next.len_utf8();
    }
    output
}

/// Replaces up to `limit` matches (`limit < 0` means all) and reports how many
/// were replaced. The empty-match step matches `matches`.
pub fn replace(pattern: &Pattern, replacement: &str, subject: &str, limit: i64) -> (String, i64) {
    let names = pattern.names();
    let mut output = String::new();
    let mut position = 0usize;
    let mut count = 0i64;
    while limit < 0 || count < limit {
        let captures = match pattern.regex.captures_from_pos(subject, position) {
            Ok(Some(captures)) => captures,
            _ => break,
        };
        let whole = captures.get(0).expect("group 0 always participates");
        let start = whole.start();
        let end = whole.end();
        output.push_str(&subject[position..start]);
        output.push_str(&expand(replacement, &captures, names));
        count += 1;
        if start == end {
            if end >= subject.len() {
                position = end;
                break;
            }
            let next = advance(subject, end);
            output.push_str(&subject[end..next]);
            position = next;
        } else {
            position = end;
        }
    }
    output.push_str(&subject[position..]);
    (output, count)
}

/// Splits on every match, keeping each separator's capture groups in place
/// (the Python `re.split` behaviour).
pub fn split_json(pattern: &Pattern, subject: &str, max_split: i64) -> String {
    let mut items: Vec<Json> = Vec::new();
    let mut position = 0usize;
    let mut segment_start = 0usize;
    let mut splits = 0i64;
    while max_split < 0 || splits < max_split {
        let captures = match pattern.regex.captures_from_pos(subject, position) {
            Ok(Some(captures)) => captures,
            _ => break,
        };
        let whole = captures.get(0).expect("group 0 always participates");
        let start = whole.start();
        let end = whole.end();
        items.push(Json::String(subject[segment_start..start].to_string()));
        for index in 1..=pattern.group_count() {
            items.push(Json::String(group_text(&captures, index)));
        }
        splits += 1;
        segment_start = end;
        if start == end {
            if end >= subject.len() {
                break;
            }
            position = advance(subject, end);
        } else {
            position = end;
        }
    }
    items.push(Json::String(subject[segment_start..].to_string()));
    dump(&Json::Array(items))
}

const ESCAPED: &str = "()[]{}?*+-|^$\\.&~# \t\n\r\u{b}\u{c}";

/// Escapes every character the previous engine escaped, including the space.
pub fn escape(text: &str) -> String {
    let mut output = String::new();
    for character in text.chars() {
        if ESCAPED.contains(character) {
            output.push('\\');
        }
        output.push(character);
    }
    output
}

fn escape_literal(character: char) -> String {
    let mut output = String::new();
    if ESCAPED.contains(character) {
        output.push('\\');
    }
    output.push(character);
    output
}

/// `*`, `?` and `[abc]` to an anchored regex, escaping everything else.
pub fn glob_to_regex(glob: &str) -> String {
    let mut escaped = String::new();
    let mut index = 0usize;
    let characters: Vec<char> = glob.chars().collect();
    while index < characters.len() {
        let character = characters[index];
        if character == '*' {
            escaped.push_str(".*");
        } else if character == '?' {
            escaped.push('.');
        } else if character == '[' {
            match characters[index..].iter().position(|&c| c == ']') {
                Some(offset) => {
                    let group: String = characters[index..index + offset + 1].iter().collect();
                    escaped.push_str(&group);
                    index += offset + 1;
                    continue;
                }
                None => escaped.push_str(&escape_literal(character)),
            }
        } else {
            escaped.push_str(&escape_literal(character));
        }
        index += 1;
    }
    format!("^{escaped}$")
}

/// The head of the subject, at most `max_len` bytes — the answer
/// `truncateMatch` gives when the pattern does not compile.
pub fn truncated_head(subject: &str, max_len: i64) -> String {
    let mut end = (max_len.max(0) as usize).min(subject.len());
    while end > 0 && !subject.is_char_boundary(end) {
        end -= 1;
    }
    subject[..end].to_string()
}

/// A `max_len`-byte window centred on the first match (or the head of the
/// subject when there is none).
pub fn truncate_match(pattern: &Pattern, subject: &str, max_len: i64) -> String {
    let max_len = max_len.max(0) as usize;
    let found = match pattern.regex.find(subject) {
        Ok(Some(found)) => found,
        _ => return truncated_head(subject, max_len as i64),
    };
    let half = (max_len / 2) as usize;
    let start = found.start().saturating_sub(half);
    let end = (start + max_len).min(subject.len());
    if end <= start {
        return String::new();
    }
    // The window edges may fall inside a multi-byte character; snap them.
    let mut start = start;
    while !subject.is_char_boundary(start) {
        start += 1;
    }
    let mut end = end;
    while end > start && !subject.is_char_boundary(end) {
        end -= 1;
    }
    subject[start..end].to_string()
}

/// The start offset of the first match, or `-1`.
pub fn first_match_pos(pattern: &Pattern, subject: &str) -> i64 {
    match pattern.regex.find(subject) {
        Ok(Some(found)) => found.start() as i64,
        _ => -1,
    }
}

/// The end offset of the first match, or `-1`.
pub fn match_end_pos(pattern: &Pattern, subject: &str) -> i64 {
    match pattern.regex.find(subject) {
        Ok(Some(found)) => found.end() as i64,
        _ => -1,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn spans(source: &str, flags: u32, subject: &str) -> Vec<(usize, usize)> {
        let pattern = Pattern::compile(source, flags).expect("pattern compiles");
        matches(&pattern, subject)
            .iter()
            .map(|record| (record.start, record.end))
            .collect()
    }

    fn replacement(source: &str, replacement: &str, subject: &str) -> String {
        let pattern = Pattern::compile(source, 0).expect("pattern compiles");
        replace(&pattern, replacement, subject, -1).0
    }

    // The `regex` crate's own iterators suppress an empty match at the position
    // a previous non-empty match ended. Python and ECMAScript do not, and the
    // module contract is Python's.
    #[test]
    fn empty_matches_follow_python() {
        assert_eq!(spans("a?", 0, "banana").len(), 7);
        assert_eq!(spans("a*", 0, "baaac"), vec![(0, 0), (1, 4), (4, 4), (5, 5)]);
        assert_eq!(spans("", 0, "abc"), vec![(0, 0), (1, 1), (2, 2), (3, 3)]);
    }

    // Iteration must search the whole subject, or `^` would match the start of
    // every suffix and lookbehind could not see the character before it.
    #[test]
    fn anchors_and_lookbehind_see_the_whole_subject() {
        assert_eq!(spans("^", MULTILINE, "a\nb"), vec![(0, 0), (2, 2)]);
        assert_eq!(spans("(?<=a)b", 0, "ab ab"), vec![(1, 2), (4, 5)]);
        assert_eq!(spans("\\b", 0, "hi there").len(), 4);
    }

    #[test]
    fn newly_supported_constructs_compile_and_match() {
        for (source, subject) in [
            ("(?<=a)b", "ab"),
            ("(?>a+)b", "aab"),
            ("a++b", "aab"),
            ("\\p{Greek}+", "\u{3b1}\u{3b2}"),
            ("a(?i)bc", "aBC"),
            ("(?P<w>\\w)(?P=w)", "aa"),
        ] {
            let pattern = Pattern::compile(source, 0).expect(source);
            assert!(pattern.is_match(subject), "{source} should match {subject}");
        }
    }

    #[test]
    fn verbose_mode_is_honoured() {
        let pattern = Pattern::compile("a  b", VERBOSE).expect("compiles");
        assert!(pattern.is_match("ab"));
        assert!(!pattern.is_match("a  b"));
    }

    #[test]
    fn replacement_understands_python_backreferences() {
        assert_eq!(replacement("(\\d+)", "[\\1]", "a1b22"), "a[1]b[22]");
        assert_eq!(replacement("(?P<w>\\w+)", "[\\g<w>]", "hi there"), "[hi] [there]");
        assert_eq!(replacement("(a)(b)", "\\2\\1", "ab"), "ba");
        // A literal `$` survives, `\g` without a name is literal, and an
        // undefined number expands to nothing.
        assert_eq!(replacement("x", "a$b", "x"), "a$b");
        assert_eq!(replacement("(a)", "<\\g1>", "a"), "<g1>");
        assert_eq!(replacement("(a)", "\\10", "a"), "");
    }

    #[test]
    fn split_keeps_capture_groups() {
        let pattern = Pattern::compile("(,)", 0).expect("compiles");
        assert_eq!(split_json(&pattern, "a,b", -1), "[\"a\", \",\", \"b\"]");
        let empty = Pattern::compile("", 0).expect("compiles");
        assert_eq!(split_json(&empty, "ab", -1), "[\"\", \"a\", \"b\", \"\"]");
    }

    #[test]
    fn escape_and_glob_are_unchanged() {
        assert_eq!(escape("a.b+c"), "a\\.b\\+c");
        assert_eq!(escape("hello world"), "hello\\ world");
        assert_eq!(glob_to_regex("*.txt"), "^.*\\.txt$");
        assert_eq!(glob_to_regex("a[bc]d"), "^a[bc]d$");
    }

    #[test]
    fn json_matches_native_json_formatting() {
        let value = Json::Object(vec![
            ("user".to_string(), Json::String("alice".to_string())),
            ("host".to_string(), Json::Null),
            (
                "tags".to_string(),
                Json::Array(vec![Json::String("a\"b".to_string()), Json::Integer(3)]),
            ),
        ]);
        assert_eq!(
            dump(&value),
            "{\"user\": \"alice\", \"host\": null, \"tags\": [\"a\\\"b\", 3]}"
        );
    }
}
