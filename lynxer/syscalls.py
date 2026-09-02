"""Linux syscall dispatch behind Lynxer's named ``syscall*`` built-ins.

Syscall numbers are resolved for the host architecture with the
``system_calls`` tables and the calls are issued through ``ctypes``, so
supporting a syscall is one table entry here rather than one wrapper in the
native C++ extension.

Numbers and argument widths are always taken from the host: an x86-64 build
never resolves arm64 numbers and a 32-bit host never passes 64-bit words.
"""

from __future__ import annotations

import ctypes
import os
import sys
from typing import Sequence

try:
    import system_calls
except ImportError:  # pragma: no cover - every build installs it
    system_calls = None

MAX_SYSCALL_ARGS = 6

# ``uname`` machine names that differ from the ``system_calls`` table names.
# A 64-bit arm64 kernel and an x86-64 kernel must each pick their own table,
# and 32-bit userspace on those CPUs (armv8l, i686) must pick the 32-bit one.
_ARCH_ALIASES = {
    "amd64": "x86_64",
    "x86-64": "x86_64",
    "aarch64": "arm64",
    "armv8l": "arm",
    "armv8b": "arm",
    "armv7l": "arm",
    "armv6l": "arm",
    "i486": "i386",
    "i586": "i386",
    "i686": "i386",
    "ppc64le": "powerpc64",
    "ppc": "powerpc",
}

# A syscall argument is one machine word.  Masking to the host word size keeps
# pointers readable by the kernel on 32-bit hosts instead of handing it a
# 64-bit value split across two argument slots.
WORD_BYTES = ctypes.sizeof(ctypes.c_void_p)
_WORD_BITS = WORD_BYTES * 8
_WORD_MASK = (1 << _WORD_BITS) - 1
_WORD_MIN = -(1 << (_WORD_BITS - 1))
_WORD_TYPE = ctypes.c_ulonglong if WORD_BYTES >= 8 else ctypes.c_uint32

# Lynxer built-in name -> Linux syscall name.  ``lynxer.builtins`` derives the
# built-in names from this table, keeping it the single source of truth.
SYSCALL_TABLE: dict[str, str] = {
    "syscallRead": "read",
    "syscallWrite": "write",
    "syscallOpenAt": "openat",
    "syscallClose": "close",
    "syscallReadVector": "readv",
    "syscallWriteVector": "writev",
    "syscallSeekFile": "lseek",
    "syscallGetFileStatus": "fstat",
    "syscallGetFileStatusAt": "newfstatat",
    "syscallTruncateFile": "ftruncate",
    "syscallSynchronizeFile": "fsync",
    "syscallSynchronizeFileData": "fdatasync",
    "syscallDuplicateFileDescriptor": "dup",
    "syscallDuplicateFileDescriptorAt": "dup3",
    "syscallCreatePipe": "pipe2",
    "syscallControlFileDescriptor": "fcntl",
    "syscallGetDirectoryEntries": "getdents64",
    "syscallReadSymbolicLink": "readlinkat",
    "syscallCreateDirectoryAt": "mkdirat",
    "syscallRemoveFileAt": "unlinkat",
    "syscallRenameFileAt": "renameat",
    "syscallCreateHardLinkAt": "linkat",
    "syscallCreateSymbolicLinkAt": "symlinkat",
    "syscallChangeFilePermissions": "fchmodat",
    "syscallChangeFileDescriptorPermissions": "fchmod",
    "syscallChangeFileOwner": "fchownat",
    "syscallChangeFileDescriptorOwner": "fchown",
    "syscallMemoryMap": "mmap",
    "syscallMemoryUnmap": "munmap",
    "syscallMemoryProtect": "mprotect",
    "syscallMemoryAdvise": "madvise",
    "syscallMemoryRemap": "mremap",
    "syscallAdjustProgramBreak": "brk",
    "syscallExecuteProgram": "execve",
    "syscallExecuteProgramAt": "execveat",
    "syscallExitProcess": "exit",
    "syscallExitAllThreads": "exit_group",
    "syscallWaitForProcess": "wait4",
    "syscallGetProcessId": "getpid",
    "syscallGetParentProcessId": "getppid",
    "syscallSendSignal": "kill",
    "syscallCreateThread": "clone",
    "syscallGetThreadId": "gettid",
    "syscallWaitOnMemory": "futex",
    "syscallSetThreadIdAddress": "set_tid_address",
    "syscallSetRobustThreadList": "set_robust_list",
    "syscallGetRobustThreadList": "get_robust_list",
    "syscallYieldProcessor": "sched_yield",
    "syscallGetClockTime": "clock_gettime",
    "syscallGetClockResolution": "clock_getres",
    "syscallSleep": "nanosleep",
    "syscallGetRandomBytes": "getrandom",
    "syscallCreateSocket": "socket",
    "syscallCreateSocketPair": "socketpair",
    "syscallBindSocket": "bind",
    "syscallListenSocket": "listen",
    "syscallAcceptConnection": "accept",
    "syscallConnectSocket": "connect",
    "syscallSendData": "sendto",
    "syscallReceiveData": "recvfrom",
    "syscallSendMessage": "sendmsg",
    "syscallReceiveMessage": "recvmsg",
    "syscallShutdownSocket": "shutdown",
    "syscallGetSocketAddress": "getsockname",
    "syscallGetPeerAddress": "getpeername",
    "syscallSetSocketOption": "setsockopt",
    "syscallGetSocketOption": "getsockopt",
    "syscallPollFileDescriptors": "poll",
    "syscallCreateEventPoll": "epoll_create1",
    "syscallControlEventPoll": "epoll_ctl",
    "syscallWaitForEvents": "epoll_wait",
    "syscallGetSystemInformation": "sysinfo",
    "syscallGetResourceUsage": "getrusage",
    "syscallGetResourceLimit": "getrlimit",
    "syscallSetResourceLimit": "setrlimit",
    "syscallControlProcess": "prctl",
}


def host_architecture() -> str:
    """Return the ``system_calls`` table name for the host architecture."""
    machine = os.uname().machine.lower() if hasattr(os, "uname") else sys.platform
    return _ARCH_ALIASES.get(machine, machine)


def _load_libc() -> ctypes.CDLL:
    libc = ctypes.CDLL(None, use_errno=True)
    try:
        libc.syscall
    except AttributeError:
        raise RuntimeError("this platform has no syscall(2) wrapper in libc") from None
    # ``syscall`` is variadic: declaring every argument as a machine word keeps
    # pointer-sized and negative arguments intact instead of truncating them
    # to ``int``.
    libc.syscall.argtypes = [ctypes.c_long] + [_WORD_TYPE] * MAX_SYSCALL_ARGS
    libc.syscall.restype = ctypes.c_long
    return libc


_libc: ctypes.CDLL | None = None
_table = None
_cache: dict[str, int] = {}


def _syscall_number(name: str) -> int:
    """Return the host architecture's number for a Linux syscall name."""
    global _table
    number = _cache.get(name)
    if number is not None:
        return number
    if system_calls is None:
        raise RuntimeError("the 'system-calls' package is required for named syscalls")
    if _table is None:
        _table = system_calls.syscalls()
    architecture = host_architecture()
    try:
        number = _table.get(name, architecture)
    except system_calls.NoSuchSystemCall:
        raise RuntimeError(f"unknown Linux syscall '{name}'") from None
    except system_calls.NotSupportedSystemCall:
        raise RuntimeError(
            f"syscall '{name}' is not available on architecture {architecture}"
        ) from None
    except system_calls.NoSuchArchitecture:
        raise RuntimeError(
            f"syscalls are not available on architecture {architecture}"
        ) from None
    _cache[name] = number
    return number


def unavailable() -> list[str]:
    """Return the built-ins the host architecture cannot dispatch.

    Numbers and availability differ per architecture, so this is what the
    current host is missing rather than a property of the table itself.
    """
    missing = []
    for builtin, name in SYSCALL_TABLE.items():
        try:
            _syscall_number(name)
        except (RuntimeError, NotImplementedError):
            missing.append(builtin)
    return sorted(missing)


def _encode(arg: int) -> int:
    """Encode one argument as a machine word for the kernel."""
    if not _WORD_MIN <= arg <= _WORD_MASK:
        raise ValueError(
            f"syscall argument {arg} does not fit in a {_WORD_BITS}-bit word"
        )
    return arg & _WORD_MASK


def invoke(builtin: str, args: Sequence[int]) -> int:
    """Invoke the syscall behind a built-in and return its raw result.

    Arguments are passed as machine words, so callers hand in native addresses
    and already-encoded negative values.  A ``-1`` result is raised as
    :class:`OSError` carrying the Linux ``errno``.
    """
    global _libc
    if not sys.platform.startswith("linux"):
        raise NotImplementedError("named syscalls are only available on Linux")
    if len(args) > MAX_SYSCALL_ARGS:
        raise ValueError("syscalls accept at most six arguments")
    number = _syscall_number(SYSCALL_TABLE[builtin])
    if _libc is None:
        _libc = _load_libc()
    # ctypes requires every declared argument, so unused slots are padded.
    values = [_encode(arg) for arg in args]
    values.extend([0] * (MAX_SYSCALL_ARGS - len(values)))
    ctypes.set_errno(0)
    result = _libc.syscall(number, *values)
    if result == -1:
        error = ctypes.get_errno()
        if error:
            raise OSError(error, os.strerror(error))
    return result
