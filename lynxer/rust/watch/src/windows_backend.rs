//! Windows backend: `ReadDirectoryChangesW` with overlapped I/O.
//!
//! A background thread waits on the overlapped event, parses the
//! `FILE_NOTIFY_INFORMATION` records and queues Lynxer events. `watchWait`
//! blocks on a condition variable with the interpreter lock released, so a
//! Windows watch never wedges another Lynxer thread. `watchFd` has no useful
//! descriptor on Windows (`asyncPoll*` is POSIX-only) and returns `-1`; use
//! `watchWait`/`watchDrain` instead.
//!
//! This backend is compiled only for Windows; the Linux test suite exercises
//! the inotify backend, and the Windows CI job exercises this one.

use std::collections::HashMap;
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicI64, Ordering};
use std::sync::{Arc, Condvar, Mutex, OnceLock};
use std::thread;
use std::time::Duration;

use windows_sys::Win32::Foundation::{
    CloseHandle, HANDLE, WAIT_OBJECT_0,
};
use windows_sys::Win32::Storage::FileSystem::{
    CreateFileW, ReadDirectoryChangesW, FILE_FLAG_BACKUP_SEMANTICS,
    FILE_FLAG_OVERLAPPED, FILE_LIST_DIRECTORY, FILE_NOTIFY_CHANGE_CREATION,
    FILE_NOTIFY_CHANGE_DIR_NAME, FILE_NOTIFY_CHANGE_FILE_NAME,
    FILE_NOTIFY_CHANGE_LAST_WRITE, FILE_NOTIFY_CHANGE_SIZE, FILE_SHARE_DELETE,
    FILE_SHARE_READ, FILE_SHARE_WRITE, OPEN_EXISTING,
};
use windows_sys::Win32::System::IO::{
    CancelIoEx, GetOverlappedResult, OVERLAPPED,
};
use windows_sys::Win32::System::Threading::{
    CreateEventW, SetEvent, WaitForSingleObject, INFINITE,
};

use crate::events::{merge, to_json, WatchedEvent};

type Queue = Arc<(Mutex<Vec<WatchedEvent>>, Condvar)>;

struct Watch {
    stop: Arc<AtomicBool>,
    queue: Queue,
    thread: Option<thread::JoinHandle<()>>,
    directory: usize,
    event: usize,
    debounce_ms: u64,
    last: HashMap<String, u64>,
}

fn registry() -> &'static Mutex<HashMap<i64, Watch>> {
    static WATCHES: OnceLock<Mutex<HashMap<i64, Watch>>> = OnceLock::new();
    WATCHES.get_or_init(|| Mutex::new(HashMap::new()))
}

fn wide(path: &str) -> Vec<u16> {
    path.encode_utf16().chain(std::iter::once(0)).collect()
}

const NOTIFY_FILTER: u32 = FILE_NOTIFY_CHANGE_FILE_NAME
    | FILE_NOTIFY_CHANGE_DIR_NAME
    | FILE_NOTIFY_CHANGE_LAST_WRITE
    | FILE_NOTIFY_CHANGE_SIZE
    | FILE_NOTIFY_CHANGE_CREATION;

fn action_kind(action: u32) -> &'static str {
    match action {
        1 => "create",      // FILE_ACTION_ADDED
        2 => "delete",      // FILE_ACTION_REMOVED
        3 => "modify",      // FILE_ACTION_MODIFIED
        4 => "moved_from",  // FILE_ACTION_RENAMED_OLD_NAME
        5 => "moved_to",    // FILE_ACTION_RENAMED_NEW_NAME
        _ => "other",
    }
}

fn parse_buffer(buffer: &[u8], root: &str) -> Vec<WatchedEvent> {
    let mut events = Vec::new();
    let mut offset = 0usize;
    while offset + 12 <= buffer.len() {
        let next = u32::from_ne_bytes([
            buffer[offset],
            buffer[offset + 1],
            buffer[offset + 2],
            buffer[offset + 3],
        ]) as usize;
        let action = u32::from_ne_bytes([
            buffer[offset + 4],
            buffer[offset + 5],
            buffer[offset + 6],
            buffer[offset + 7],
        ]);
        let name_length = u32::from_ne_bytes([
            buffer[offset + 8],
            buffer[offset + 9],
            buffer[offset + 10],
            buffer[offset + 11],
        ]) as usize;
        let name_start = offset + 12;
        let name_end = name_start.saturating_add(name_length);
        if name_end > buffer.len() {
            break;
        }
        let mut name = Vec::with_capacity(name_length / 2);
        let mut index = name_start;
        while index + 1 < name_end {
            name.push(u16::from_ne_bytes([buffer[index], buffer[index + 1]]));
            index += 2;
        }
        let name = String::from_utf16_lossy(&name);
        let path = Path::new(root)
            .join(name)
            .to_string_lossy()
            .into_owned();
        events.push(WatchedEvent {
            path,
            kind: action_kind(action).to_string(),
        });
        if next == 0 {
            break;
        }
        offset = offset.saturating_add(next);
    }
    events
}

type Ready = Arc<(Mutex<Option<bool>>, Condvar)>;

fn worker(
    directory: usize,
    event: usize,
    recursive: bool,
    stop: Arc<AtomicBool>,
    queue: Queue,
    ready: Ready,
    root: String,
) {
    let directory = directory as HANDLE;
    let event = event as HANDLE;
    // 64 KiB is the documented maximum for a single notification batch. An
    // 8-byte element type keeps the buffer aligned for the record structs.
    let mut buffer = vec![0u64; 8192];
    loop {
        if stop.load(Ordering::Relaxed) {
            break;
        }
        let mut overlapped: OVERLAPPED = unsafe { std::mem::zeroed() };
        overlapped.hEvent = event;
        let started = unsafe {
            ReadDirectoryChangesW(
                directory,
                buffer.as_mut_ptr().cast(),
                (buffer.len() * std::mem::size_of::<u64>()) as u32,
                if recursive { 1 } else { 0 },
                NOTIFY_FILTER,
                std::ptr::null_mut(),
                &mut overlapped,
                None,
            )
        };
        {
            let (lock, signal) = &*ready;
            if let Ok(mut state) = lock.lock() {
                if state.is_none() {
                    *state = Some(started != 0);
                    signal.notify_all();
                }
            }
        }
        if started == 0 {
            break;
        }
        if unsafe { WaitForSingleObject(event, INFINITE) } != WAIT_OBJECT_0 {
            break;
        }
        let mut transferred = 0u32;
        let completed = unsafe {
            GetOverlappedResult(directory, &overlapped, &mut transferred, 0)
        };
        if completed == 0 || transferred == 0 {
            if stop.load(Ordering::Relaxed) {
                break;
            }
            continue;
        }
        let bytes = unsafe {
            std::slice::from_raw_parts(
                buffer.as_ptr().cast::<u8>(),
                transferred as usize,
            )
        };
        let events = parse_buffer(bytes, &root);
        if !events.is_empty() {
            let (lock, signal) = &*queue;
            if let Ok(mut pending) = lock.lock() {
                pending.extend(events);
                signal.notify_all();
            }
        }
    }
}

pub fn add_impl(path: &str, recursive: bool) -> i64 {
    let wide_path = wide(path);
    let directory = unsafe {
        CreateFileW(
            wide_path.as_ptr(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            std::ptr::null(),
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            std::ptr::null_mut(),
        )
    };
    if directory.is_null() || directory == windows_sys::Win32::Foundation::INVALID_HANDLE_VALUE {
        return -1;
    }
    let event = unsafe { CreateEventW(std::ptr::null(), 1, 0, std::ptr::null()) };
    if event.is_null() {
        unsafe { CloseHandle(directory) };
        return -1;
    }
    let stop = Arc::new(AtomicBool::new(false));
    let queue: Queue = Arc::new((Mutex::new(Vec::new()), Condvar::new()));
    let ready: Ready = Arc::new((Mutex::new(None), Condvar::new()));
    let thread = {
        let stop = Arc::clone(&stop);
        let queue = Arc::clone(&queue);
        let ready = Arc::clone(&ready);
        let root = path.to_string();
        let directory_handle = directory as usize;
        let event_handle = event as usize;
        thread::spawn(move || {
            worker(
                directory_handle,
                event_handle,
                recursive,
                stop,
                queue,
                ready,
                root,
            )
        })
    };
    // Do not return until the first overlapped read is armed; otherwise a
    // change made immediately after `watchAdd` can be missed.
    {
        let (lock, signal) = &*ready;
        let mut state = lock.lock().unwrap();
        while state.is_none() {
            state = signal.wait(state).unwrap();
        }
        if *state == Some(false) {
            stop.store(true, Ordering::Relaxed);
            unsafe {
                CancelIoEx(directory, std::ptr::null());
                SetEvent(event);
                CloseHandle(event);
                CloseHandle(directory);
            }
            let _ = thread.join();
            return -1;
        }
    }
    static NEXT: AtomicI64 = AtomicI64::new(1);
    let handle = NEXT.fetch_add(1, Ordering::Relaxed);
    registry().lock().unwrap().insert(
        handle,
        Watch {
            stop,
            queue,
            thread: Some(thread),
            directory: directory as usize,
            event: event as usize,
            debounce_ms: 0,
            last: HashMap::new(),
        },
    );
    handle
}

pub fn fd_of(_handle: i64) -> i64 {
    // The interpreter's async poller is POSIX-only; Windows callers use
    // `watchWait`, which blocks on a condition variable.
    -1
}

pub fn wait_impl(handle: i64, timeout_ms: i64) -> i64 {
    let queue = {
        let watches = registry().lock().unwrap();
        match watches.get(&handle) {
            Some(watch) => Arc::clone(&watch.queue),
            None => return -1,
        }
    };
    let (lock, signal) = &*queue;
    let pending = lock.lock().unwrap();
    if !pending.is_empty() {
        return 1;
    }
    if timeout_ms < 0 {
        let pending = signal.wait(pending).unwrap();
        return if pending.is_empty() { -1 } else { 1 };
    }
    let deadline = std::time::Instant::now()
        .checked_add(Duration::from_millis(timeout_ms as u64));
    let mut pending = pending;
    loop {
        if !pending.is_empty() {
            return 1;
        }
        let remaining = match deadline {
            Some(deadline) => {
                let now = std::time::Instant::now();
                if now >= deadline {
                    return 0;
                }
                deadline - now
            }
            None => return 0,
        };
        let (next, timeout) = signal.wait_timeout(pending, remaining).unwrap();
        pending = next;
        if timeout.timed_out() && pending.is_empty() {
            return 0;
        }
    }
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

pub fn drain_impl(handle: i64) -> String {
    let mut watches = registry().lock().unwrap();
    let watch = match watches.get_mut(&handle) {
        Some(watch) => watch,
        None => return String::new(),
    };
    let (lock, _) = &*watch.queue;
    let events = match lock.lock() {
        Ok(mut pending) => std::mem::take(&mut *pending),
        Err(_) => return String::new(),
    };
    let merged = merge(events, watch.debounce_ms, &mut watch.last);
    to_json(&merged)
}

pub fn remove_impl(handle: i64) -> bool {
    let removed = registry().lock().unwrap().remove(&handle);
    match removed {
        Some(mut watch) => {
            watch.stop.store(true, Ordering::Relaxed);
            let directory = watch.directory as HANDLE;
            let event = watch.event as HANDLE;
            // Wake a `watchWait` blocked on the queue as well as the I/O
            // worker blocked on the overlapped event.
            {
                let (lock, signal) = &*watch.queue;
                if let Ok(_pending) = lock.lock() {
                    signal.notify_all();
                }
            }
            unsafe {
                CancelIoEx(directory, std::ptr::null());
                SetEvent(event);
            }
            if let Some(thread) = watch.thread.take() {
                let _ = thread.join();
            }
            unsafe {
                CloseHandle(event);
                CloseHandle(directory);
            }
            true
        }
        None => false,
    }
}
