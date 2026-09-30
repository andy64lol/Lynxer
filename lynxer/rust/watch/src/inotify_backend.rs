//! Linux backend: inotify.
//!
//! inotify is not recursive, so `add_recursive` registers every subdirectory.
//! The descriptor is put in non-blocking mode so `watchDrain` never blocks;
//! `watchWait` polls it with the interpreter lock released.

use std::collections::HashMap;
use std::os::fd::AsRawFd;
use std::path::Path;
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::{Mutex, OnceLock};

use inotify::{EventMask, Inotify, WatchMask};

use crate::events::{merge, to_json, WatchedEvent};

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

pub fn add_impl(path: &str, recursive: bool) -> i64 {
    let mut inotify = match Inotify::init() {
        Ok(inotify) => inotify,
        Err(_) => return -1,
    };
    if add_recursive(&mut inotify, Path::new(path), recursive).is_err() {
        return -1;
    }
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

pub fn fd_of(handle: i64) -> i64 {
    registry()
        .lock()
        .unwrap()
        .get(&handle)
        .map(|watch| watch.inotify.as_raw_fd() as i64)
        .unwrap_or(-1)
}

pub fn set_debounce_impl(handle: i64, milliseconds: i64) -> bool {
    let mut watches = registry().lock().unwrap();
    match watches.get_mut(&handle) {
        Some(watch) => {
            watch.debounce_ms = milliseconds.max(0) as u64;
            true
        }
        None => false,
    }
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

pub fn drain_impl(handle: i64) -> String {
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
    let merged = merge(events, watch.debounce_ms, &mut watch.last);
    to_json(&merged)
}

pub fn remove_impl(handle: i64) -> bool {
    // Dropping the `Inotify` closes its descriptor and removes every watch.
    registry().lock().unwrap().remove(&handle).is_some()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(name: &str) -> std::path::PathBuf {
        let dir = std::env::temp_dir().join(format!("lynxer_watch_{name}_{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn add_drain_remove() {
        let dir = scratch("drain");
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
