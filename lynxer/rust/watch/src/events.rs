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
