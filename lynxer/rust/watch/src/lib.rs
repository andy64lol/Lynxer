//! Lynxer `watch` stdlib backend: filesystem change events over inotify.
//!
//! Each watch is an integer handle. `watchFd` returns the inotify descriptor so
//! a program can wait on it with the interpreter's own poll set
//! (`asyncPollRegister` / `asyncPollWait`) instead of blocking inside the
//! module: a module cannot release the interpreter lock, so a blocking wait here
//! would wedge every other Lynxer thread. After the descriptor is ready,
//! `watchDrain` reads the pending events as a JSON array.
//!
//! Failure is a scalar sentinel: `watchAdd` returns `-1`, `watchFd` returns
//! `-1`, `watchDrain` returns `""` for an unknown handle, and the predicates
//! return `false`.

use std::collections::HashMap;
use std::os::fd::AsRawFd;
use std::path::Path;
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::{Mutex, OnceLock};

use inotify::{EventMask, Inotify, WatchMask};
use lynxer_abi::{export_int, export_string, lynxer_module};
use serde::Serialize;

struct Watch {
    inotify: Inotify,
    root: String,
    debounce_ms: u64,
    last: HashMap<String, u64>,
}

fn registry() -> &'static Mutex<HashMap<i64, Watch>> {
    static WATCHES: OnceLock<Mutex<HashMap<i64, Watch>>> = OnceLock::new();
    WATCHES.get_or_init(|| Mutex::new(HashMap::new()))
}

fn watch_mask() -> WatchMask {
    WatchMask::CREATE
        | WatchMask::DELETE
        | WatchMask::MODIFY
        | WatchMask::MOVED_FROM
        | WatchMask::MOVED_TO
        | WatchMask::CLOSE_WRITE
        | WatchMask::ATTRIB
}

/// Add a watch, recursing into subdirectories (inotify is not recursive).
fn add_recursive(
    inotify: &mut Inotify,
    path: &Path,
    recursive: bool,
) -> Result<(), std::io::Error> {
    inotify.watches().add(path, watch_mask())?;
    if recursive && path.is_dir() {
        for entry in std::fs::read_dir(path)? {
            let entry = entry?;
            let child = entry.path();
            if child.is_dir() {
                add_recursive(inotify, &child, true)?;
            }
        }
    }
    Ok(())
}

fn add_impl(path: &str, recursive: bool) -> i64 {
    let mut inotify = match Inotify::init() {
        Ok(inotify) => inotify,
        Err(_) => return -1,
    };
    if add_recursive(&mut inotify, Path::new(path), recursive).is_err() {
        return -1;
    }
    // Non-blocking, so `watchDrain` returns immediately when there is nothing.
    let flags = unsafe { libc::fcntl(inotify.as_raw_fd(), libc::F_GETFL) };
    unsafe { libc::fcntl(inotify.as_raw_fd(), libc::F_SETFL, flags | libc::O_NONBLOCK) };
    static NEXT: AtomicI64 = AtomicI64::new(1);
    let handle = NEXT.fetch_add(1, Ordering::Relaxed);
    registry().lock().unwrap().insert(
        handle,
        Watch {
            inotify,
            root: path.to_string(),
            debounce_ms: 0,
            last: HashMap::new(),
        },
    );
    handle
}

fn fd_of(handle: i64) -> i64 {
    registry()
        .lock()
        .unwrap()
        .get(&handle)
        .map(|watch| watch.inotify.as_raw_fd() as i64)
        .unwrap_or(-1)
}

fn set_debounce_impl(handle: i64, milliseconds: i64) -> bool {
    let mut watches = registry().lock().unwrap();
    match watches.get_mut(&handle) {
        Some(watch) => {
            watch.debounce_ms = milliseconds.max(0) as u64;
            true
        }
        None => false,
    }
}

#[derive(Clone, Serialize)]
struct WatchedEvent {
    path: String,
    kind: String,
}

fn event_kind(mask: EventMask) -> &'static str {
    if mask.contains(EventMask::CREATE) {
        "create"
    } else if mask.contains(EventMask::DELETE) {
        "delete"
    } else if mask.contains(EventMask::MOVED_FROM) {
        "moved_from"
    } else if mask.contains(EventMask::MOVED_TO) {
        "moved_to"
    } else if mask.contains(EventMask::CLOSE_WRITE) || mask.contains(EventMask::MODIFY) {
        "modify"
    } else if mask.contains(EventMask::ATTRIB) {
        "attrib"
    } else {
        "other"
    }
}

fn now_millis() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_millis() as u64)
        .unwrap_or(0)
}

fn drain_impl(handle: i64) -> String {
    let mut watches = registry().lock().unwrap();
    let watch = match watches.get_mut(&handle) {
        Some(watch) => watch,
        None => return String::new(),
    };
    let root = watch.root.clone();
    let mut buffer = [0u8; 16384];
    let mut events: Vec<WatchedEvent> = Vec::new();
    loop {
        match watch.inotify.read_events(&mut buffer) {
            Ok(pending) => {
                let mut read_any = false;
                for event in pending {
                    read_any = true;
                    let name = event
                        .name
                        .map(|name| name.to_string_lossy().into_owned())
                        .unwrap_or_default();
                    let path = if name.is_empty() {
                        root.clone()
                    } else {
                        format!("{}/{}", root.trim_end_matches('/'), name)
                    };
                    events.push(WatchedEvent {
                        path,
                        kind: event_kind(event.mask).to_string(),
                    });
                }
                if !read_any {
                    break;
                }
            }
            Err(_) => break,
        }
    }
    // Coalesce a repeated path within the debounce window, keeping the last kind.
    let mut merged: Vec<WatchedEvent> = Vec::new();
    if watch.debounce_ms > 0 {
        let now = now_millis();
        for event in events {
            let suppressed = watch
                .last
                .get(&event.path)
                .map(|seen| now.saturating_sub(*seen) < watch.debounce_ms)
                .unwrap_or(false);
            if suppressed {
                if let Some(previous) = merged.iter_mut().find(|kept| kept.path == event.path) {
                    previous.kind = event.kind;
                } else {
                    merged.push(event.clone());
                }
            } else {
                watch.last.insert(event.path.clone(), now);
                merged.push(event);
            }
        }
    } else {
        merged = events;
    }
    serde_json::to_string(&merged).unwrap_or_default()
}

fn remove_impl(handle: i64) -> bool {
    let removed = registry().lock().unwrap().remove(&handle);
    match removed {
        // Dropping the `Inotify` closes its descriptor and removes every watch.
        Some(_watch) => true,
        None => false,
    }
}

export_int!(watch_add, args, {
    add_impl(args.string(0), args.int(0) != 0)
});

export_int!(watch_fd, args, { fd_of(args.int(0)) });

export_string!(watch_drain, args, { drain_impl(args.int(0)) });

export_int!(watch_set_debounce, args, {
    set_debounce_impl(args.int(0), args.int(1)) as i64
});

export_int!(watch_remove, args, { remove_impl(args.int(0)) as i64 });

const OPS: &[(&str, &str, &str)] = &[
    ("add", "watch_add", "cdecl:int64(...)"),
    ("fd", "watch_fd", "cdecl:int64(...)"),
    ("drain", "watch_drain", "cdecl:cstring(...)"),
    ("setDebounce", "watch_set_debounce", "cdecl:int64(...)"),
    ("remove", "watch_remove", "cdecl:int64(...)"),
];

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add_drain_remove() {
        let dir = std::env::temp_dir().join(format!("lynxer_watch_test_{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let handle = add_impl(dir.to_str().unwrap(), false);
        assert!(handle > 0);
        assert!(fd_of(handle) >= 0);
        std::fs::write(dir.join("a.txt"), b"hi").unwrap();
        std::thread::sleep(std::time::Duration::from_millis(200));
        let drained = drain_impl(handle);
        assert!(drained.starts_with('['));
        assert!(drained.contains("a.txt"));
        assert!(remove_impl(handle));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn unknown_handle_is_sentinel() {
        assert_eq!(fd_of(9999), -1);
        assert_eq!(drain_impl(9999), "");
        assert!(!remove_impl(9999));
    }
}
