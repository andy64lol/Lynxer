# Linux arm64 syscalls

Import the native-backed `linux-arm64` standard library under an identifier
with `importAs`:

```lynx
fn setup() {
    importAs("linux-arm64", "linuxArm64");
}

fn main() {
    println(linuxArm64.syscallGetProcessId());
}
```

The module accepts calls only on Linux arm64; it does not cross-dispatch to
another operating system or architecture. Its wrapper catalog, arguments, and
raw ABI requirements are shared with the
[Linux amd64 API](linux-amd64.md) and listed in the
[syscall reference](../syscalls.md).

## arm64-specific polling

| Lynxer wrapper | Linux interface | Notes |
| --- | --- | --- |
| `syscallPollFileDescriptors(fds, count, timeoutMs)` | `ppoll` | Portable wrapper converts milliseconds to a `timespec`. |
| `syscallPpollFileDescriptors(fds, count, timespec, sigmask, sigsetsize)` | `ppoll` | Explicit signal-mask form. |
| `syscallWaitForEvents(epoll, events, maxevents, timeout, sigmask)` | `epoll_pwait` | `epoll_wait` is not available on arm64. |
| `syscallWaitForEventsWithSignalMask(epoll, events, maxevents, timeout, sigmask)` | `epoll_pwait` | Lynxer supplies the signal-set size for the kernel ABI. |
| `syscallWaitForEvents2(epoll, events, maxevents, timeout, sigmask, size)` | `epoll_pwait2` | Timeout is a `timespec` address. |

All other wrappers pass their integer arguments as raw syscall arguments. The
Linux kernel headers available at build time determine whether a named syscall
is present; unavailable names raise an error instead of dispatching a
different syscall number.

See the [`linux-arm64` module reference](../stdlib/linux-arm64.md) and the
[shared syscall catalog](../syscalls.md).
