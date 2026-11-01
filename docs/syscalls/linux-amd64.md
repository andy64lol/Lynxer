# Linux amd64 syscalls

Import the native-backed `linux-amd64` standard library under an identifier
with `importAs`:

```lynx
fn setup() {
    importAs("linux-amd64", "linuxAmd64");
}

fn main() {
    println(linuxAmd64.syscallGetProcessId());
}
```

The module accepts calls only on Linux amd64; it does not cross-dispatch to
another operating system or architecture. Its wrapper catalog, arguments, and
raw ABI requirements are shared with the
[Linux arm64 API](linux-arm64.md) and listed in the
[syscall reference](../syscalls.md).

## amd64-specific polling

| Lynxer wrapper | Linux interface | Notes |
| --- | --- | --- |
| `syscallPollFileDescriptors(fds, count, timeoutMs)` | `poll` | Timeout is milliseconds; negative values wait indefinitely. |
| `syscallPpollFileDescriptors(fds, count, timespec, sigmask, sigsetsize)` | `ppoll` | Explicit signal-mask form. |
| `syscallWaitForEvents(epoll, events, maxevents, timeout, sigmask)` | `epoll_wait` | Uses the four-argument amd64 syscall ABI. |
| `syscallWaitForEventsWithSignalMask(epoll, events, maxevents, timeout, sigmask)` | `epoll_pwait` | Lynxer supplies the signal-set size for the kernel ABI. |
| `syscallWaitForEvents2(epoll, events, maxevents, timeout, sigmask, size)` | `epoll_pwait2` | Timeout is a `timespec` address. |

All other wrappers pass their integer arguments as raw syscall arguments. The
Linux kernel headers available at build time determine whether a named syscall
is present; unavailable names raise an error instead of dispatching a
different syscall number.

See the [`linux-amd64` module reference](../stdlib/linux-amd64.md) and the
[shared syscall catalog](../syscalls.md).
