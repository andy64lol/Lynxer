"""Linux syscall dispatch behind Lynxer's named ``syscall*`` built-ins.

Syscall numbers are resolved for the host architecture with the
``system_calls`` tables and the calls are issued through ``ctypes``, so
supporting a syscall is one table entry here rather than one wrapper in the
native C++ extension.

Numbers and argument widths are always taken from the host: an x86-64 build
never resolves arm64 numbers, and unsupported Python ABIs are rejected before
any call is dispatched.
"""

from __future__ import annotations

import ctypes
import os
import sys
from typing import Sequence

try:
    import system_calls
except ImportError as error:  # pragma: no cover - installation failure
    raise ImportError(
        "Lynxer requires the 'system-calls' package; "
        "install dependencies from requirements_venv.txt"
    ) from error

MAX_SYSCALL_ARGS = 6
SUPPORTED_ARCHITECTURES = frozenset({"arm64", "x86_64"})
SUPPORTED_PLATFORM = "linux"

# ``uname`` machine names that differ from the ``system_calls`` table names.
# An arm64 kernel and an x86-64 kernel must each pick their own table. The
# 32-bit aliases remain normalized for clear unsupported-platform errors.
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

# A syscall argument is one machine word. Masking to the host word size keeps
# pointers and encoded negative arguments in the native ABI representation.
WORD_BYTES = ctypes.sizeof(ctypes.c_void_p)
_WORD_BITS = WORD_BYTES * 8
_WORD_MASK = (1 << _WORD_BITS) - 1
_WORD_MIN = -(1 << (_WORD_BITS - 1))
_WORD_TYPE = ctypes.c_ulonglong if WORD_BYTES >= 8 else ctypes.c_uint32

# Lynxer built-in name -> Linux syscall name.  ``lynxer.builtins`` derives the
# built-in names from this table, keeping it the single source of truth.
SYSCALL_TABLE: dict[str, str] = {
    "syscallGetCurrentDirectory": "getcwd",
    "syscallChangeDirectory": "chdir",
    "syscallControlInputOutput": "ioctl",
    "syscallRead": "read",
    "syscallWrite": "write",
    "syscallPositionedRead64": "pread64",
    "syscallPositionedWrite64": "pwrite64",
    "syscallOpenAt": "openat",
    "syscallClose": "close",
    "syscallReadVector": "readv",
    "syscallWriteVector": "writev",
    "syscallSeekFile": "lseek",
    "syscallGetFileStatus": "fstat",
    "syscallGetFileStatusAt": "newfstatat",
    "syscallTruncateFile": "ftruncate",
    "syscallCheckFileAccessAt": "faccessat",
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
    "syscallPpollFileDescriptors": "ppoll",
    "syscallCreateEventPoll": "epoll_create1",
    "syscallControlEventPoll": "epoll_ctl",
    "syscallWaitForEvents": "epoll_wait",
    "syscallWaitForEventsWithSignalMask": "epoll_pwait",
    "syscallInitializeInodeNotifications": "inotify_init1",
    "syscallAddInodeNotificationWatch": "inotify_add_watch",
    "syscallRemoveInodeNotificationWatch": "inotify_rm_watch",
    "syscallGetSystemInformation": "sysinfo",
    "syscallGetUnixSystemName": "uname",
    "syscallGetExtendedFileStatus": "statx",
    "syscallGetResourceUsage": "getrusage",
    "syscallGetResourceLimit": "getrlimit",
    "syscallSetResourceLimit": "setrlimit",
    "syscallControlProcess": "prctl",
}

_ARCHITECTURE_SYSCALLS: dict[str, dict[str, str]] = {
    "syscallPollFileDescriptors": {
        "x86_64": "poll",
        "arm64": "ppoll",
    },
    "syscallWaitForEvents": {
        "x86_64": "epoll_wait",
        "arm64": "epoll_pwait",
    },
}

# These expose the ARM64 alternatives directly. They are deliberately not
# treated as portable aliases even on hosts whose kernel happens to provide
# the same syscall name.
_BUILTIN_ARCHITECTURES: dict[str, frozenset[str]] = {
    "syscallPpollFileDescriptors": frozenset({"arm64"}),
    "syscallWaitForEventsWithSignalMask": frozenset({"arm64"}),
}


def syscall_name_for_arch(builtin: str, architecture: str) -> str:
    """Return the Linux syscall used by a Lynxer built-in on one architecture.

    Architecture-neutral built-ins can select a kernel alternative. For
    example, ``syscallPollFileDescriptors`` selects ``poll`` on x86-64 and
    ``ppoll`` on ARM64.
    """
    if builtin not in SYSCALL_TABLE:
        raise ValueError(f"unknown syscall built-in '{builtin}'")
    alternatives = _ARCHITECTURE_SYSCALLS.get(builtin)
    if alternatives is not None and architecture in alternatives:
        return alternatives[architecture]
    return SYSCALL_TABLE[builtin]


def syscall_builtin_supported_on_arch(builtin: str, architecture: str) -> bool:
    """Return whether a named built-in is exposed on an architecture."""
    if builtin not in SYSCALL_TABLE:
        raise ValueError(f"unknown syscall built-in '{builtin}'")
    return architecture in _BUILTIN_ARCHITECTURES.get(
        builtin, frozenset(SUPPORTED_ARCHITECTURES)
    )


def syscall_argument_count(builtin: str, architecture: str) -> int | None:
    """Return the exact public argument count for ABI-adapted syscalls."""
    if builtin == "syscallPollFileDescriptors":
        return 3
    if builtin in {
        "syscallPpollFileDescriptors",
        "syscallWaitForEvents",
        "syscallWaitForEventsWithSignalMask",
    }:
        return 5
    return None


def host_architecture() -> str:
    """Return the ``system_calls`` table name for the host architecture."""
    machine = os.uname().machine.lower() if hasattr(os, "uname") else sys.platform
    return _ARCH_ALIASES.get(machine, machine)


def platform_error() -> str | None:
    """Return a clear error when this runtime cannot issue supported syscalls.

    The syscall tables and the native ``syscall(2)`` ABI are tied to the
    running Linux architecture.  In particular, a 32-bit Python process on a
    64-bit kernel must not accidentally use the 64-bit table.
    """
    if not sys.platform.startswith(SUPPORTED_PLATFORM):
        return "named syscalls require a Linux runtime"

    architecture = host_architecture()
    if architecture not in SUPPORTED_ARCHITECTURES:
        supported = ", ".join(sorted(SUPPORTED_ARCHITECTURES))
        return (
            f"unsupported Linux architecture '{architecture or 'unknown'}'; "
            f"supported architectures are {supported}"
        )
    if WORD_BYTES != 8:
        return (
            f"unsupported {WORD_BYTES * 8}-bit Python ABI on {architecture}; "
            "Lynxer syscall builds require a 64-bit Python runtime"
        )
    return None


def require_supported_platform() -> str:
    """Validate and return the normalized architecture for this runtime."""
    error = platform_error()
    if error is not None:
        raise RuntimeError(error)
    return host_architecture()


def _load_libc() -> ctypes.CDLL:
    require_supported_platform()
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
_cache: dict[tuple[str, str], int] = {}


def _syscall_number(name: str) -> int:
    """Return the host architecture's number for a Linux syscall name."""
    global _table
    architecture = require_supported_platform()
    cache_key = (architecture, name)
    number = _cache.get(cache_key)
    if number is not None:
        return number
    if _table is None:
        _table = system_calls.syscalls()
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
    _cache[cache_key] = number
    return number


def unavailable() -> list[str]:
    """Return the built-ins the host architecture cannot dispatch.

    Numbers and availability differ per architecture, so this is what the
    current host is missing rather than a property of the table itself.
    """
    missing = []
    architecture = require_supported_platform()
    for builtin in SYSCALL_TABLE:
        try:
            if not syscall_builtin_supported_on_arch(builtin, architecture):
                raise RuntimeError(
                    f"syscall built-in '{builtin}' is not available on {architecture}"
                )
            _syscall_number(syscall_name_for_arch(builtin, architecture))
        except (RuntimeError, NotImplementedError, TypeError):
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
    require_supported_platform()
    if builtin not in SYSCALL_TABLE:
        raise ValueError(f"unknown syscall built-in '{builtin}'")
    if len(args) > MAX_SYSCALL_ARGS:
        raise ValueError("syscalls accept at most six arguments")
    if any(isinstance(arg, bool) or not isinstance(arg, int) for arg in args):
        raise TypeError("syscall arguments must be integers")
    architecture = require_supported_platform()
    if not syscall_builtin_supported_on_arch(builtin, architecture):
        allowed = ", ".join(
            sorted(_BUILTIN_ARCHITECTURES.get(builtin, frozenset()))
        )
        raise RuntimeError(
            f"syscall built-in '{builtin}' is only available on {allowed}"
        )
    expected_count = syscall_argument_count(builtin, architecture)
    if expected_count is not None and len(args) != expected_count:
        raise ValueError(
            f"{builtin} expects exactly {expected_count} arguments, received {len(args)}"
        )
    syscall_name = syscall_name_for_arch(builtin, architecture)
    number = _syscall_number(syscall_name)
    if _libc is None:
        _libc = _load_libc()
    # ctypes requires every declared argument, so unused slots are padded.
    call_args = list(args)
    keepalive = None
    if syscall_name == "ppoll" and builtin == "syscallPollFileDescriptors":
        timeout_ms = call_args[2]
        if timeout_ms < 0:
            timeout_pointer = 0
        else:
            class _Timespec(ctypes.Structure):
                _fields_ = [
                    ("tv_sec", ctypes.c_long),
                    ("tv_nsec", ctypes.c_long),
                ]

            keepalive = _Timespec(timeout_ms // 1000, (timeout_ms % 1000) * 1_000_000)
            timeout_pointer = ctypes.addressof(keepalive)
        # ppoll(fds, nfds, timeout, sigmask, sigsetsize), with the same
        # millisecond timeout API exposed by the existing Lynxer built-in.
        call_args = [
            call_args[0],
            call_args[1],
            timeout_pointer,
            0,
            ctypes.sizeof(ctypes.c_ulong),
        ]
    elif builtin == "syscallWaitForEvents":
        if syscall_name == "epoll_wait":
            # The portable API accepts a signal-mask slot so its signature is
            # the same as ARM64 epoll_pwait; epoll_wait ignores that slot.
            call_args = call_args[:4]
        else:
            # Linux's raw epoll_pwait syscall has a sixth sigsetsize word.
            call_args.append(ctypes.sizeof(ctypes.c_ulong))
    elif builtin == "syscallWaitForEventsWithSignalMask":
        # Linux's raw epoll_pwait syscall has a sixth sigsetsize word.
        call_args.append(ctypes.sizeof(ctypes.c_ulong))
    values = [_encode(arg) for arg in call_args]
    values.extend([0] * (MAX_SYSCALL_ARGS - len(values)))
    ctypes.set_errno(0)
    result = _libc.syscall(number, *values)
    if result == -1:
        error = ctypes.get_errno()
        if error:
            raise OSError(error, os.strerror(error))
    return result
