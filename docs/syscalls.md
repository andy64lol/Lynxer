# Linux Syscall Built-ins

Lynxer exposes a named, low-level wrapper for each syscall in the table below.
The wrappers are implemented by the native C++ extension and select the
appropriate Linux syscall number for the host architecture.

## Calling convention

Every syscall built-in takes the syscall arguments as positional integer
values. A call accepts zero to six arguments. Pointer arguments must be native
addresses, such as addresses returned by `memoryAllocate`; strings and
structures must be prepared in native memory before calling a syscall.

The wrappers return the syscall's result as an integer. A result of `-1` is
reported as a Lynxer runtime error containing the Linux `errno` message. These
are raw Linux ABI calls: argument layouts, flags, structures, and pointer
lifetimes are the caller's responsibility. They are available on Linux only.

For example:

```c
global main(){
    println(syscallGetProcessId());
    println(syscallYieldProcessor());
}
```

## File descriptors and files

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallRead(fd, buffer, count)` | `read` | descriptor, buffer address, byte count |
| `syscallWrite(fd, buffer, count)` | `write` | descriptor, buffer address, byte count |
| `syscallOpenAt(dirfd, path, flags, mode)` | `openat` | directory descriptor, path address, flags, mode |
| `syscallClose(fd)` | `close` | descriptor |
| `syscallReadVector(fd, iov, count)` | `readv` | descriptor, iovec array address, element count |
| `syscallWriteVector(fd, iov, count)` | `writev` | descriptor, iovec array address, element count |
| `syscallSeekFile(fd, offset, whence)` | `lseek` | descriptor, offset, `SEEK_*` value |
| `syscallGetFileStatus(fd, status)` | `fstat` | descriptor, stat structure address |
| `syscallGetFileStatusAt(dirfd, path, status, flags)` | `newfstatat` | directory descriptor, path, stat address, flags |
| `syscallTruncateFile(fd, length)` | `ftruncate` | descriptor, length |
| `syscallSynchronizeFile(fd)` | `fsync` | descriptor |
| `syscallSynchronizeFileData(fd)` | `fdatasync` | descriptor |
| `syscallDuplicateFileDescriptor(fd)` | `dup` | descriptor |
| `syscallDuplicateFileDescriptorAt(fd, newfd, flags)` | `dup3` | descriptor, new descriptor, flags |
| `syscallCreatePipe(pipefd, flags)` | `pipe2` | two-int output array address, flags |
| `syscallControlFileDescriptor(fd, command, argument)` | `fcntl` | descriptor, `F_*` command, command argument |
| `syscallGetDirectoryEntries(fd, buffer, count)` | `getdents64` | descriptor, buffer address, byte count |
| `syscallReadSymbolicLink(dirfd, path, buffer, size)` | `readlinkat` | directory descriptor, path, buffer, byte count |

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
| `syscallAdjustProgramBreak(address)` | `brk` | requested program-break address |
| `syscallExecuteProgram(path, argv, envp)` | `execve` | path, argv-array, envp-array addresses |
| `syscallExecuteProgramAt(fd, path, argv, envp, flags)` | `execveat` | descriptor, path, argv, envp, flags |

## Processes and threads

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallExitProcess(status)` | `exit` | exit status; does not return |
| `syscallExitAllThreads(status)` | `exit_group` | exit status; does not return |
| `syscallWaitForProcess(pid, status, options, rusage)` | `wait4` | process ID, status address, options, rusage address |
| `syscallGetProcessId()` | `getpid` | none |
| `syscallGetParentProcessId()` | `getppid` | none |
| `syscallSendSignal(pid, signal)` | `kill` | process ID, signal |
| `syscallCreateThread(flags, stack, parent_tid, child_tid, tls)` | `clone` | clone flags and native addresses |
| `syscallGetThreadId()` | `gettid` | none |
| `syscallWaitOnMemory(address, operation, value, timeout, address2, value3)` | `futex` | futex address and futex ABI arguments |
| `syscallSetThreadIdAddress(address)` | `set_tid_address` | address |
| `syscallSetRobustThreadList(head, length)` | `set_robust_list` | list address, byte length |
| `syscallGetRobustThreadList(pid, head, length)` | `get_robust_list` | process ID, output addresses |
| `syscallYieldProcessor()` | `sched_yield` | none |

## Clocks, sleep, and randomness

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallGetClockTime(clock, timespec)` | `clock_gettime` | clock ID, timespec address |
| `syscallGetClockResolution(clock, resolution)` | `clock_getres` | clock ID, timespec address |
| `syscallSleep(request, remainder)` | `nanosleep` | timespec addresses |
| `syscallGetRandomBytes(buffer, count, flags)` | `getrandom` | buffer address, byte count, flags |

## Sockets

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallCreateSocket(domain, type, protocol)` | `socket` | socket domain, type, protocol |
| `syscallCreateSocketPair(domain, type, protocol, sockets)` | `socketpair` | domain, type, protocol, output array address |
| `syscallBindSocket(socket, address, length)` | `bind` | descriptor, socket address, length |
| `syscallListenSocket(socket, backlog)` | `listen` | descriptor, backlog |
| `syscallAcceptConnection(socket, address, length)` | `accept` | descriptor, address and length pointers |
| `syscallConnectSocket(socket, address, length)` | `connect` | descriptor, socket address, length |
| `syscallSendData(socket, buffer, length, flags, address, addressLength)` | `sendto` | descriptor, buffer, length, flags, destination, address length |
| `syscallReceiveData(socket, buffer, length, flags, address, addressLength)` | `recvfrom` | descriptor, buffer, length, flags, source, address length |
| `syscallSendMessage(socket, message, flags)` | `sendmsg` | descriptor, msghdr address, flags |
| `syscallReceiveMessage(socket, message, flags)` | `recvmsg` | descriptor, msghdr address, flags |
| `syscallShutdownSocket(socket, how)` | `shutdown` | descriptor, `SHUT_*` value |
| `syscallGetSocketAddress(socket, address, length)` | `getsockname` | descriptor, address and length pointers |
| `syscallGetPeerAddress(socket, address, length)` | `getpeername` | descriptor, address and length pointers |
| `syscallSetSocketOption(socket, level, option, value, length)` | `setsockopt` | descriptor, level, option, value address, length |
| `syscallGetSocketOption(socket, level, option, value, length)` | `getsockopt` | descriptor, level, option, output value, length pointer |

## Polling and system information

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallPollFileDescriptors(fds, count, timeout)` | `poll` | pollfd array address, count, timeout |
| `syscallCreateEventPoll(flags)` | `epoll_create1` | flags |
| `syscallControlEventPoll(epoll, operation, fd, event)` | `epoll_ctl` | epoll descriptor, `EPOLL_CTL_*`, descriptor, event address |
| `syscallWaitForEvents(epoll, events, maxevents, timeout)` | `epoll_wait` | epoll descriptor, event array, capacity, timeout |
| `syscallGetSystemInformation(info)` | `sysinfo` | sysinfo structure address |

## Resources and process control

| Built-in | Linux syscall | Arguments |
| --- | --- | --- |
| `syscallGetResourceUsage(who, usage)` | `getrusage` | `RUSAGE_*` selector, usage structure address |
| `syscallGetResourceLimit(resource, limit)` | `getrlimit` | `RLIMIT_*` selector, rlimit structure address |
| `syscallSetResourceLimit(resource, limit)` | `setrlimit` | `RLIMIT_*` selector, rlimit structure address |
| `syscallControlProcess(option, arg2, arg3, arg4, arg5)` | `prctl` | `PR_*` option and option-specific arguments |