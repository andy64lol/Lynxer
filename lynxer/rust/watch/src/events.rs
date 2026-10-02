//! Shared event type and the debounce coalescer, used by every backend.

use std::collections::HashMap;

use serde::Serialize;

#[derive(Clone, Serialize)]
pub struct WatchedEvent {
    pub path: String,
    pub kind: String,
}

pub fn now_millis() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_millis() as u64)
        .unwrap_or(0)
}

pub fn join_path(root: &str, name: &str) -> String {
    if name.is_empty() {
        return root.to_string();
    }
    if root.ends_with('/') {
        format!("{root}{name}")
    } else {
        format!("{root}/{name}")
    }
}

#[allow(dead_code)]
pub fn directory_snapshot(path: &str) -> std::collections::HashSet<String> {
    let Ok(entries) = std::fs::read_dir(path) else {
        return std::collections::HashSet::new();
    };
    entries
        .filter_map(|entry| entry.ok())
        .map(|entry| entry.file_name().to_string_lossy().into_owned())
        .collect()
}

pub fn changed_entry_paths(path: &str, before: &std::collections::HashSet<String>, after: &std::collections::HashSet<String>) -> Vec<String> {
    let mut changed = Vec::new();
    let mut added: Vec<String> = after.difference(before).cloned().collect();
    let mut removed: Vec<String> = before.difference(after).cloned().collect();
    added.sort();
    removed.sort();
    for name in added {
        changed.push(join_path(path, &name));
    }
    for name in removed {
        changed.push(join_path(path, &name));
    }
    changed
}

/// Coalesces a repeated path within the debounce window, keeping the last kind.
/// A window of 0 returns the events unchanged.
pub fn merge(
    events: Vec<WatchedEvent>,
    debounce_ms: u64,
    last: &mut HashMap<String, u64>,
) -> Vec<WatchedEvent> {
    if debounce_ms == 0 {
        return events;
    }
    let now = now_millis();
    let mut merged: Vec<WatchedEvent> = Vec::new();
    for event in events {
        let suppressed = last
            .get(&event.path)
            .map(|seen| now.saturating_sub(*seen) < debounce_ms)
            .unwrap_or(false);
        if suppressed {
            if let Some(previous) = merged.iter_mut().find(|kept| kept.path == event.path) {
                previous.kind = event.kind;
            } else {
                merged.push(event);
            }
        } else {
            last.insert(event.path.clone(), now);
            merged.push(event);
        }
    }
    merged
}

pub fn to_json(events: &[WatchedEvent]) -> String {
    serde_json::to_string(events).unwrap_or_default()
}


#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn join_path_keeps_root_and_name() {
        assert_eq!(join_path("/tmp/root", "a.txt"), "/tmp/root/a.txt");
        assert_eq!(join_path("/tmp/root/", "a.txt"), "/tmp/root/a.txt");
        assert_eq!(join_path("/tmp/root", ""), "/tmp/root");
    }

    #[test]
    fn changed_entries_report_created_and_deleted_names() {
        let before = std::collections::HashSet::from(["a.txt".to_string(), "b.txt".to_string()]);
        let after = std::collections::HashSet::from(["b.txt".to_string(), "c.txt".to_string()]);
        let mut changed = changed_entry_paths("/tmp/root", &before, &after);
        changed.sort();
        assert_eq!(changed, vec!["/tmp/root/a.txt".to_string(), "/tmp/root/c.txt".to_string()]);
    }
}
