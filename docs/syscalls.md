# Named syscalls

Lynxer exposes one named built-in per Linux syscall. They are **raw ABI calls**:
argument layouts, flags, structures and pointer lifetimes are the caller's
responsibility. Every wrapper is **architecture-gated** and reached through the
architecture's namespace.

```lynx
global setup(){ syscalls("amd64"); }

global main(){
    println(amd64.syscallGetProcessId());
    println(amd64.syscallYieldProcessor());
}
```

## Selecting an architecture

`syscalls("<keyword>")` must run before any syscall call; it declares which
architecture the program targets.

| Keyword | Selects |
| --- | --- |
| `amd64`, `x86-64` | x86-64 |
| `arm64`, `aarch64` | AArch64 |

- The keyword must name the **host** machine. `syscalls("arm64")` on an amd64
  machine is an error, not a cross-architecture dispatch.
- Only **one architecture at a time** may be selected; re-selecting the same one
  is a no-op, a different one is an error.
- A misspelled keyword or namespace prefix reports the closest match, e.g.
  `unknown syscall architecture 'amd6'. You meant: amd64?`.
- `global.sys.architecture()` (see [stdlib/sys.md](stdlib/sys.md)) returns the
  keyword for this machine, so `syscalls(global.sys.architecture())` makes a
  program assert it is running on the architecture its call prefixes assume.
- The former flat `syscallRead(...)` spelling is gone; it fails with a pointer to
  the namespaced form.

Calls then use the matching namespace — `amd64.syscallRead(...)` or
`arm64.syscallRead(...)`. A call through the wrong namespace is an error.

## Calling convention

- Arguments are positional **integers**, zero to six of them.
- Pointer arguments must be native addresses, such as those returned by
  `memoryAllocate`; strings and structures are prepared in native memory first.
- The result is the syscall's return value as an integer. A result of `-1` is
  raised as a source-located error carrying the Linux `errno` message, e.g.
  `syscall 'syscallRead' failed: Bad file descriptor`.
- These wrappers require a Linux runtime. A name whose number is missing from the
  build's headers fails with `syscall '<name>' is not available on this
  architecture` instead of dispatching the wrong table.

## Portable `poll`/`epoll`

`poll(2)` and `epoll_wait(2)` do not exist on AArch64, so the two portable
wrappers dispatch to whichever the host has, while the explicit `ppoll` /
`epoll_pwait` forms are available on **both** architectures:

| Built-in | amd64 | aarch64 |
| --- | --- | --- |
| `syscallPollFileDescriptors` | `poll` | `ppoll` |
| `syscallPpollFileDescriptors` | `ppoll` | `ppoll` |
| `syscallWaitForEvents` | `epoll_wait` | `epoll_pwait` |
| `syscallWaitForEventsWithSignalMask` | `epoll_pwait` | `epoll_pwait` |

## Working directories

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallGetCurrentDirectory(buffer, size)` | `getcwd` | output buffer address, buffer capacity |
| `syscallChangeDirectory(path)` | `chdir` | NUL-terminated path address |

## File descriptors and files

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallRead(fd, buffer, count)` | `read` | descriptor, buffer address, byte count |
| `syscallWrite(fd, buffer, count)` | `write` | descriptor, buffer address, byte count |
| `syscallPositionedRead64(fd, buffer, count, offset)` | `pread64` | descriptor, buffer address, byte count, byte offset |
| `syscallPositionedWrite64(fd, buffer, count, offset)` | `pwrite64` | descriptor, buffer address, byte count, byte offset |
| `syscallOpenAt(dirfd, path, flags, mode)` | `openat` | directory descriptor, path address, flags, mode |
| `syscallOpenAt2(dirfd, path, resolve, size)` | `openat2` | directory descriptor, path, `open_how` address, size |
| `syscallClose(fd)` | `close` | descriptor |
| `syscallReadVector(fd, iov, count)` | `readv` | descriptor, iovec array address, element count |
| `syscallWriteVector(fd, iov, count)` | `writev` | descriptor, iovec array address, element count |
| `syscallSeekFile(fd, offset, whence)` | `lseek` | descriptor, offset, `SEEK_*` value |
| `syscallGetFileStatus(fd, status)` | `fstat` | descriptor, stat structure address |
| `syscallGetFileStatusAt(dirfd, path, status, flags)` | `newfstatat` | directory descriptor, path, stat address, flags |
| `syscallGetExtendedFileStatus(dirfd, path, flags, mask, status)` | `statx` | directory descriptor, path, flags, mask, `statx` address |
| `syscallTruncateFile(fd, length)` | `ftruncate` | descriptor, length |
| `syscallFallocateFile(fd, mode, offset, length)` | `fallocate` | descriptor, mode, byte range |
| `syscallCheckFileAccessAt(dirfd, path, mode, flags)` | `faccessat` | directory descriptor, path address, access mode, flags |
| `syscallCheckFileAccessAt2(dirfd, path, mode, flags)` | `faccessat2` | as `faccessat`, with flags honoured |
| `syscallCopyFileRange(fdin, offin, fdout, offout, length, flags)` | `copy_file_range` | input/output descriptors, offsets, length, flags |
| `syscallSynchronizeFile(fd)` | `fsync` | descriptor |
| `syscallSynchronizeFileData(fd)` | `fdatasync` | descriptor |
| `syscallSynchronizeFilesystem(fd)` | `syncfs` | any descriptor on the target filesystem |
| `syscallDuplicateFileDescriptor(fd)` | `dup` | descriptor |
| `syscallDuplicateFileDescriptorAt(fd, newfd, flags)` | `dup3` | descriptor, new descriptor, flags |
| `syscallCreatePipe(pipefd, flags)` | `pipe2` | two-int output array address, flags |
| `syscallControlFileDescriptor(fd, command, argument)` | `fcntl` | descriptor, `F_*` command, command argument |
| `syscallGetDirectoryEntries(fd, buffer, count)` | `getdents64` | descriptor, buffer address, byte count |
| `syscallReadSymbolicLink(dirfd, path, buffer, size)` | `readlinkat` | directory descriptor, path, buffer, byte count |
| `syscallControlInputOutput(fd, request, argument)` | `ioctl` | descriptor, request code, request-specific argument |

## Directory entries, links, ownership, and permissions

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallCreateDirectoryAt(dirfd, path, mode)` | `mkdirat` | directory descriptor, path address, mode |
| `syscallRemoveFileAt(dirfd, path, flags)` | `unlinkat` | directory descriptor, path address, flags |
| `syscallRenameFileAt(oldfd, oldpath, newfd, newpath)` | `renameat` | directory descriptors and path addresses |
| `syscallCreateHardLinkAt(olddirfd, oldpath, newdirfd, newpath, flags)` | `linkat` | directory descriptors, path addresses, flags |
| `syscallCreateSymbolicLinkAt(target, newdirfd, linkpath)` | `symlinkat` | target address, directory descriptor, link path address |
| `syscallChangeFilePermissions(dirfd, path, mode)` | `fchmodat` | directory descriptor, path, mode |
| `syscallChangeFileDescriptorPermissions(fd, mode)` | `fchmod` | descriptor, mode |
| `syscallChangeFileOwner(dirfd, path, owner, group, flags)` | `fchownat` | directory descriptor, path, owner, group, flags |
| `syscallChangeFileDescriptorOwner(fd, owner, group)` | `fchown` | descriptor, owner, group |

## Memory and program execution

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallMemoryMap(address, length, protection, flags, fd, offset)` | `mmap` | address, length, `PROT_*`, `MAP_*`, descriptor, offset |
| `syscallMemoryUnmap(address, length)` | `munmap` | address, length |
| `syscallMemoryProtect(address, length, protection)` | `mprotect` | address, length, `PROT_*` |
| `syscallMemoryAdvise(address, length, advice)` | `madvise` | address, length, `MADV_*` |
| `syscallMemoryRemap(oldaddress, oldsize, newsize, flags, newaddress)` | `mremap` | old address, old size, new size, flags, new address |
| `syscallSynchronizeMemory(address, length, flags)` | `msync` | mapped address, length, `MS_*` flags |
| `syscallAdjustProgramBreak(address)` | `brk` | requested program-break address |
| `syscallExecuteProgram(path, argv, envp)` | `execve` | path, argv-array, envp-array addresses |
| `syscallExecuteProgramAt(fd, path, argv, envp, flags)` | `execveat` | descriptor, path, argv, envp, flags |
| `syscallLockMemory(address, length)` | `mlock` | address, length |
| `syscallUnlockMemory(address, length)` | `munlock` | address, length |
| `syscallCreateMemoryFileDescriptor(name, flags)` | `memfd_create` | name address, flags |
| `syscallSetMemoryPolicy(address, length, mode, nodemask, maxnode, flags)` | `mbind` | NUMA placement; both arches |

## Processes and threads

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallExitProcess(status)` | `exit` | exit status; does not return |
| `syscallExitAllThreads(status)` | `exit_group` | exit status; does not return |
| `syscallWaitForProcess(pid, status, options, rusage)` | `wait4` | process ID, status address, options, rusage address |
| `syscallWaitForProcessId(idtype, id, info, options, rusage)` | `waitid` | id type, id, siginfo address, options, rusage |
| `syscallGetProcessId()` | `getpid` | none |
| `syscallGetParentProcessId()` | `getppid` | none |
| `syscallSendSignal(pid, signal)` | `kill` | process ID, signal |
| `syscallOpenProcessFileDescriptor(pid, flags)` | `pidfd_open` | process ID, flags |
| `syscallSendSignalToProcessFileDescriptor(pidfd, signal, info, flags)` | `pidfd_send_signal` | pidfd, signal, siginfo address, flags |
| `syscallCreateThread(flags, stack, parent_tid, child_tid, tls)` | `clone` | clone flags and native addresses |
| `syscallCreateThread3(args, size)` | `clone3` | `clone_args` address, size |
| `syscallGetThreadId()` | `gettid` | none |
| `syscallWaitOnMemory(address, operation, value, timeout, address2, value3)` | `futex` | futex address and futex ABI arguments |
| `syscallSetThreadIdAddress(address)` | `set_tid_address` | address |
| `syscallSetRobustThreadList(head, length)` | `set_robust_list` | list address, byte length |
| `syscallGetRobustThreadList(pid, head, length)` | `get_robust_list` | process ID, output addresses |
| `syscallGetThreadAffinity(pid, size, mask)` | `sched_getaffinity` | process ID, mask size, output mask |
| `syscallSetThreadAffinity(pid, size, mask)` | `sched_setaffinity` | process ID, mask size, mask address |
| `syscallGetThreadPriority(which, who)` | `getpriority` | `PRIO_*` selector, id |
| `syscallSetThreadPriority(which, who, priority)` | `setpriority` | `PRIO_*` selector, id, priority |
| `syscallYieldProcessor()` | `sched_yield` | none |

## Clocks, sleep, and randomness

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallGetClockTime(clock, timespec)` | `clock_gettime` | clock ID, timespec address |
| `syscallGetClockResolution(clock, resolution)` | `clock_getres` | clock ID, timespec address |
| `syscallGetTimeOfDay(timeval, timezone)` | `gettimeofday` | timeval address, timezone address |
| `syscallSleep(request, remainder)` | `nanosleep` | timespec addresses |
| `syscallSleepClock(clock, flags, request, remainder)` | `clock_nanosleep` | clock ID, flags, timespec addresses |
| `syscallCreateTimerFileDescriptor(clock, flags)` | `timerfd_create` | clock ID, flags |
| `syscallControlTimerFileDescriptor(fd, flags, newvalue, oldvalue)` | `timerfd_settime` | descriptor, flags, itimerspec addresses |
| `syscallGetRandomBytes(buffer, count, flags)` | `getrandom` | buffer address, byte count, flags |

## Signals

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallControlSignal(signal, action, oldaction, size)` | `rt_sigaction` | signal, `sigaction` addresses, signal-set size |
| `syscallControlSignalMask(how, set, oldset, size)` | `rt_sigprocmask` | `SIG_*` how, signal-set addresses, size |
| `syscallCreateSignalFileDescriptor(fd, mask, size, flags)` | `signalfd4` | descriptor, mask, signal-set size, flags |

## Sockets

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallCreateSocket(domain, type, protocol)` | `socket` | socket domain, type, protocol |
| `syscallCreateSocketPair(domain, type, protocol, sockets)` | `socketpair` | domain, type, protocol, output array address |
| `syscallBindSocket(socket, address, length)` | `bind` | descriptor, socket address, length |
| `syscallListenSocket(socket, backlog)` | `listen` | descriptor, backlog |
| `syscallAcceptConnection(socket, address, length)` | `accept` | descriptor, address and length pointers |
| `syscallAcceptConnection4(socket, address, length, flags)` | `accept4` | as `accept`, with flags |
| `syscallConnectSocket(socket, address, length)` | `connect` | descriptor, socket address, length |
| `syscallSendData(socket, buffer, length, flags, address, addressLength)` | `sendto` | descriptor, buffer, length, flags, destination, address length |
| `syscallReceiveData(socket, buffer, length, flags, address, addressLength)` | `recvfrom` | descriptor, buffer, length, flags, source, address length |
| `syscallSendMessage(socket, message, flags)` | `sendmsg` | descriptor, msghdr address, flags |
| `syscallReceiveMessage(socket, message, flags)` | `recvmsg` | descriptor, msghdr address, flags |
| `syscallSendMessages(socket, messages, count, flags)` | `sendmmsg` | descriptor, mmsghdr array, count, flags |
| `syscallReceiveMessages(socket, messages, count, flags)` | `recvmmsg` | descriptor, mmsghdr array, count, flags |
| `syscallShutdownSocket(socket, how)` | `shutdown` | descriptor, `SHUT_*` value |
| `syscallGetSocketAddress(socket, address, length)` | `getsockname` | descriptor, address and length pointers |
| `syscallGetPeerAddress(socket, address, length)` | `getpeername` | descriptor, address and length pointers |
| `syscallSetSocketOption(socket, level, option, value, length)` | `setsockopt` | descriptor, level, option, value address, length |
| `syscallGetSocketOption(socket, level, option, value, length)` | `getsockopt` | descriptor, level, option, output value, length pointer |

## Polling and event loops

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallPollFileDescriptors(fds, count, timeout_ms)` | `poll` / `ppoll` | pollfd array address, count, timeout in milliseconds |
| `syscallPpollFileDescriptors(fds, count, timespec, sigmask, sigsetsize)` | `ppoll` | pollfd array, count, timespec address, signal-mask address, signal-set size |
| `syscallCreateEventPoll(flags)` | `epoll_create1` | flags |
| `syscallControlEventPoll(epoll, operation, fd, event)` | `epoll_ctl` | epoll descriptor, `EPOLL_CTL_*`, descriptor, event address |
| `syscallWaitForEvents(epoll, events, maxevents, timeout, sigmask)` | `epoll_wait` / `epoll_pwait` | epoll descriptor, event array, capacity, timeout, signal-mask address |
| `syscallWaitForEventsWithSignalMask(epoll, events, maxevents, timeout, sigmask)` | `epoll_pwait` | as above; Lynxer supplies the signal-set size |
| `syscallWaitForEvents2(epoll, events, maxevents, timeout, sigmask, size)` | `epoll_pwait2` | nanosecond timeout |
| `syscallCreateEventFileDescriptor(initval, flags)` | `eventfd2` | initial counter, flags |
| `syscallInitializeInodeNotifications(flags)` | `inotify_init1` | `IN_NONBLOCK`/`IN_CLOEXEC` flags |
| `syscallAddInodeNotificationWatch(inotify, path, mask)` | `inotify_add_watch` | inotify descriptor, path address, event mask |
| `syscallRemoveInodeNotificationWatch(inotify, watch)` | `inotify_rm_watch` | inotify descriptor, watch descriptor |

## System information and resources

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallGetSystemInformation(info)` | `sysinfo` | sysinfo structure address |
| `syscallGetUnixSystemName(name)` | `uname` | `utsname` structure address |
| `syscallGetSystemTimes(buffer)` | `times` | tms structure address (or 0) |
| `syscallGetResourceUsage(who, usage)` | `getrusage` | `RUSAGE_*` selector, usage structure address |
| `syscallGetResourceLimit(resource, limit)` | `getrlimit` | `RLIMIT_*` selector, rlimit structure address |
| `syscallSetResourceLimit(resource, limit)` | `setrlimit` | `RLIMIT_*` selector, rlimit structure address |
| `syscallGetCapabilities(header, data)` | `capget` | capability header and data addresses |
| `syscallSetCapabilities(header, data)` | `capset` | capability header and data addresses |
| `syscallControlProcess(option, arg2, arg3, arg4, arg5)` | `prctl` | `PR_*` option and option-specific arguments |

## Async I/O (io_uring)

Raw passthrough; build the `io_uring_params` and submission structures in native
memory and pass their addresses. Available on both architectures.

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallSetupIoUring(entries, params)` | `io_uring_setup` | ring entries, `io_uring_params` address |
| `syscallEnterIoUring(fd, to_submit, min_complete, flags, sig, sigsz)` | `io_uring_enter` | ring descriptor and submission arguments |
| `syscallRegisterIoUring(fd, opcode, arg, nr_args)` | `io_uring_register` | ring descriptor, opcode, argument, count |

## Sandboxing (Landlock and seccomp)

Raw passthrough. The `seccomp` syscall is unguarded: operation `0` installs
`SECCOMP_MODE_STRICT`, which terminates a process that issues anything outside
its tiny allowlist.

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallCreateLandlockRuleset(attr, size, flags)` | `landlock_create_ruleset` | ruleset attribute address, size, flags |
| `syscallAddLandlockRule(ruleset, type, attr, flags)` | `landlock_add_rule` | ruleset descriptor, rule type, rule attribute address, flags |
| `syscallRestrictLandlockSelf(ruleset, flags)` | `landlock_restrict_self` | ruleset descriptor, flags |
| `syscallControlSeccomp(operation, flags, args)` | `seccomp` | operation, flags, filter address |

## Fixtures

- `lynxer/examples/lowlevel_syscalls.lynx`, `lowlevel_arch.lynx`,
  `syscall_extended.lynx` and `syscall_io_uring.lynx` are architecture-agnostic:
  they carry a `__ARCH__` token that the test harness fills in
  (`SYSCALL_ARCH`, from `uname -m`).
- `lynxer/examples/amd64Syscalls.lynx` and `arm64Syscalls.lynx` are pinned to one
  target and run under `make testLynxerAmd64Syscalls` /
  `make testLynxerArm64Syscalls`.

## See also

- [builtins.md](builtins.md#syscalls) — the summary table and the gate rules.
- [stdlib/sys.md](stdlib/sys.md) — `global.sys.architecture()`, the keyword to
  pass.
- [legacy-surface.md](legacy-surface.md) — how the original flat naming maps
  onto the namespaced form.
- [builtins.md](builtins.md) — the `memoryAllocate` / `memoryWrite*` builtins
  used to prepare buffers and structures for these calls.
