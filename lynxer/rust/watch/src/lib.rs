//! Lynxer `watch` stdlib backend: filesystem change events.
//!
//! Each watch is an integer handle over a platform backend: inotify on Linux
//! and kqueue `EVFILT_VNODE` on macOS and the BSDs. `watchFd` returns the
//! backend descriptor, so a program can wait on it with the interpreter's own
//! poll set (`asyncPollRegister` / `asyncPollWait`); `watchWait` blocks on the
//! same descriptor with the interpreter lock released, so it does not wedge
//! every other Lynxer thread. After either, `watchDrain` reads the pending
//! events as a JSON array.
//!
//! Failure is a scalar sentinel: `watchAdd`/`watchFd` return `-1`,
//! `watchDrain` returns `""` for an unknown handle, `watchWait` returns `-1`,
//! and the predicates return `false`.

mod events;
mod host;

#[cfg(target_os = "linux")]
mod inotify_backend;
#[cfg(not(target_os = "linux"))]
mod kqueue_backend;

#[cfg(target_os = "linux")]
use inotify_backend as backend;
#[cfg(not(target_os = "linux"))]
use kqueue_backend as backend;

use lynxer_abi::{export_int, export_string, lynxer_module};

pub use host::lynxer_module_attach_v1;

/// Blocks until the watch's descriptor is readable, or the timeout elapses.
/// Returns 1 when readable, 0 on timeout, and -1 for an unknown handle or a
/// poll error. The interpreter lock is released while blocked. `timeout_ms < 0`
/// waits without a limit.
fn wait_impl(handle: i64, timeout_ms: i64) -> i64 {
    let descriptor = backend::fd_of(handle);
    if descriptor < 0 {
        return -1;
    }
    let mut result = -1i64;
    host::with_lock_released(|| {
        let mut poll_fd = libc::pollfd {
            fd: descriptor as i32,
            events: libc::POLLIN,
            revents: 0,
        };
        let timeout = if timeout_ms < 0 {
            -1
        } else {
            timeout_ms.min(i32::MAX as i64) as i32
        };
        let status = unsafe { libc::poll(&mut poll_fd, 1, timeout) };
        result = if status < 0 {
            -1
        } else if status == 0 {
            0
        } else {
            1
        };
    });
    result
}

export_int!(watch_add, args, {
    backend::add_impl(args.string(0), args.int(0) != 0)
});

export_int!(watch_fd, args, { backend::fd_of(args.int(0)) });

export_string!(watch_drain, args, { backend::drain_impl(args.int(0)) });

export_int!(watch_wait, args, { wait_impl(args.int(0), args.int(1)) });

export_int!(watch_set_debounce, args, {
    backend::set_debounce_impl(args.int(0), args.int(1)) as i64
});

export_int!(watch_remove, args, {
    backend::remove_impl(args.int(0)) as i64
});

const OPS: &[(&str, &str, &str)] = &[
    ("add", "watch_add", "cdecl:int64(...)"),
    ("fd", "watch_fd", "cdecl:int64(...)"),
    ("drain", "watch_drain", "cdecl:cstring(...)"),
    ("wait", "watch_wait", "cdecl:int64(...)"),
    ("setDebounce", "watch_set_debounce", "cdecl:int64(...)"),
    ("remove", "watch_remove", "cdecl:int64(...)"),
];

lynxer_module!(OPS);

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
    fn unknown_handle_is_sentinel() {
        assert_eq!(backend::fd_of(9999), -1);
        assert_eq!(backend::drain_impl(9999), "");
        assert_eq!(wait_impl(9999, 0), -1);
        assert!(!backend::remove_impl(9999));
    }

    #[test]
    fn wait_times_out_without_events() {
        let dir = scratch("wait");
        let handle = backend::add_impl(dir.to_str().unwrap(), false);
        assert_eq!(wait_impl(handle, 0), 0);
        assert!(backend::remove_impl(handle));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn wait_reports_readiness() {
        let dir = scratch("ready");
        let handle = backend::add_impl(dir.to_str().unwrap(), false);
        std::fs::write(dir.join("ready.txt"), b"hi").unwrap();
        assert_eq!(wait_impl(handle, 2000), 1);
        assert!(backend::remove_impl(handle));
        let _ = std::fs::remove_dir_all(&dir);
    }
}
