//! macOS/BSD backend: kqueue `EVFILT_VNODE`.
//!
//! kqueue reports a vnode event against the *descriptor that was registered*,
//! not a file name, so an event is reported for the watched path itself (a
//! directory reports that its contents changed). New subdirectories are
//! registered when a directory reports a write, which is the usual way to
//! approximate a recursive watch. The kqueue descriptor is a real file
//! descriptor, so `watchFd`/`watchWait` work as on Linux.
//!
//! This backend cannot be built or exercised on the Linux CI host; it is
//! `#cfg`-gated so the Linux build is unaffected.

use std::collections::HashMap;
use std::os::fd::RawFd;
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::{Mutex, OnceLock};

use crate::events::{merge, to_json, WatchedEvent};

struct Watch {
    kqueue: RawFd,
    recursive: bool,
    debounce_ms: u64,
    last: HashMap<String, u64>,
    /// Registered descriptors: fd -> (path, is_directory).
    paths: HashMap<RawFd, (String, bool)>,
}

fn registry() -> &'static Mutex<HashMap<i64, Watch>> {
    static WATCHES: OnceLock<Mutex<HashMap<i64, Watch>>> = OnceLock::new();
    WATCHES.get_or_init(|| Mutex::new(HashMap::new()))
}

fn open_flags() -> libc::c_int {
    #[cfg(target_os = "macos")]
    {
        libc::O_EVTONLY
    }
    #[cfg(not(target_os = "macos"))]
    {
        libc::O_RDONLY
    }
}

fn note_flags() -> u32 {
    (libc::NOTE_WRITE
        | libc::NOTE_DELETE
        | libc::NOTE_RENAME
        | libc::NOTE_EXTEND
        | libc::NOTE_ATTRIB) as u32
}

/// Opens `path` and registers its vnode with the kqueue. Returns the descriptor
/// or -1 on failure.
fn register(watch: &mut Watch, path: &str) -> RawFd {
    let Ok(c_path) = std::ffi::CString::new(path) else {
        return -1;
    };
    let descriptor = unsafe { libc::open(c_path.as_ptr(), open_flags()) };
    if descriptor < 0 {
        return -1;
    }
    let mut change: libc::kevent = unsafe { std::mem::zeroed() };
    change.ident = descriptor as libc::uintptr_t;
    change.filter = libc::EVFILT_VNODE;
    change.flags = libc::EV_ADD | libc::EV_CLEAR;
    change.fflags = note_flags();
    let status = unsafe {
        libc::kevent(
            watch.kqueue,
            &change,
            1,
            std::ptr::null_mut(),
            0,
            std::ptr::null(),
        )
    };
    if status < 0 {
        unsafe { libc::close(descriptor) };
        return -1;
    }
    let is_directory = std::path::Path::new(path).is_dir();
    watch
        .paths
        .insert(descriptor, (path.to_string(), is_directory));
    descriptor
}

/// Registers `path` and, when recursive, every subdirectory beneath it.
fn register_recursive(watch: &mut Watch, path: &str, recursive: bool) {
    if register(watch, path) < 0 {
        return;
    }
    if recursive && std::path::Path::new(path).is_dir() {
        if let Ok(entries) = std::fs::read_dir(path) {
            for entry in entries.flatten() {
                let child = entry.path();
                if child.is_dir() {
                    register_recursive(watch, &child.to_string_lossy(), true);
                }
            }
        }
    }
}

/// Re-registers subdirectories that appeared under `directory` since the last
/// scan (a write event on a directory can mean a new child).
fn rescan(watch: &mut Watch, directory: &str) {
    let Ok(entries) = std::fs::read_dir(directory) else {
        return;
    };
    let mut children: Vec<String> = Vec::new();
    for entry in entries.flatten() {
        let child = entry.path();
        if child.is_dir() {
            children.push(child.to_string_lossy().into_owned());
        }
    }
    let known: std::collections::HashSet<String> =
        watch.paths.values().map(|(path, _)| path.clone()).collect();
    for child in children {
        if !known.contains(&child) {
            register_recursive(watch, &child, true);
        }
    }
}

pub fn add_impl(path: &str, recursive: bool) -> i64 {
    let kqueue = unsafe { libc::kqueue() };
    if kqueue < 0 {
        return -1;
    }
    let mut watch = Watch {
        kqueue,
        recursive,
        debounce_ms: 0,
        last: HashMap::new(),
        paths: HashMap::new(),
    };
    register_recursive(&mut watch, path, recursive);
    if watch.paths.is_empty() {
        unsafe { libc::close(kqueue) };
        return -1;
    }
    static NEXT: AtomicI64 = AtomicI64::new(1);
    let handle = NEXT.fetch_add(1, Ordering::Relaxed);
    registry().lock().unwrap().insert(handle, watch);
    handle
}

pub fn fd_of(handle: i64) -> i64 {
    registry()
        .lock()
        .unwrap()
        .get(&handle)
        .map(|watch| watch.kqueue as i64)
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

fn event_kind(fflags: u32) -> &'static str {
    if fflags & (libc::NOTE_DELETE as u32) != 0 {
        "delete"
    } else if fflags & (libc::NOTE_RENAME as u32) != 0 {
        "moved_from"
    } else if fflags & ((libc::NOTE_WRITE | libc::NOTE_EXTEND) as u32) != 0 {
        "modify"
    } else if fflags & (libc::NOTE_ATTRIB as u32) != 0 {
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
    let mut buffer: Vec<libc::kevent> = vec![unsafe { std::mem::zeroed() }; 64];
    let timeout = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    let count = unsafe {
        libc::kevent(
            watch.kqueue,
            std::ptr::null(),
            0,
            buffer.as_mut_ptr(),
            buffer.len() as libc::c_int,
            &timeout,
        )
    };
    let mut events: Vec<WatchedEvent> = Vec::new();
    let mut directories_to_rescan: Vec<String> = Vec::new();
    for index in 0..count.max(0) as usize {
        let event = &buffer[index];
        let descriptor = event.ident as RawFd;
        let kind = event_kind(event.fflags).to_string();
        if let Some((path, is_directory)) = watch.paths.get(&descriptor).cloned() {
            if is_directory && watch.recursive && kind == "modify" {
                directories_to_rescan.push(path.clone());
            }
            events.push(WatchedEvent { path, kind });
        }
    }
    for directory in directories_to_rescan {
        rescan(watch, &directory);
    }
    let merged = merge(events, watch.debounce_ms, &mut watch.last);
    to_json(&merged)
}

pub fn remove_impl(handle: i64) -> bool {
    let removed = registry().lock().unwrap().remove(&handle);
    match removed {
        Some(watch) => {
            for descriptor in watch.paths.keys() {
                unsafe { libc::close(*descriptor) };
            }
            unsafe { libc::close(watch.kqueue) };
            true
        }
        None => false,
    }
}
