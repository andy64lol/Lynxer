"""Lynxer built-in functions, implementations, and runtime registry.

The interpreter value types live in :mod:`lynxer.lynxer`.  This module is
imported after those types have been defined, so it can own the complete
implementation of every built-in without making the runtime import cycle
fragile.
"""

from __future__ import annotations

import importlib
import itertools
import json
import os
import select
import stat
import subprocess
import sys
import atexit
import socket
import threading
import asyncio
import time
from collections.abc import Callable
from typing import Any, TypeVar, cast

_runtime = importlib.import_module(".lynxer", package=__package__)
_MEMORY_LIB = importlib.import_module(".cpp", package=__package__)


BaseFunction = _runtime.BaseFunction
CoroutineValue = _runtime.CoroutineValue
List = _runtime.List
LynxTuple = _runtime.LynxTuple
VarGroup = _runtime.VarGroup
Sentinel = _runtime.Sentinel
ObjectValue = _runtime.ObjectValue
Number = _runtime.Number
Address = _runtime.Address
FunctionAddress = _runtime.FunctionAddress
NativeHandle = _runtime.NativeHandle
RTError = _runtime.RTError
RTResult = _runtime.RTResult
String = _runtime.String
type_matches = _runtime.type_matches
value_type_name = _runtime.value_type_name
_get_cython_inline = _runtime._get_cython_inline

_MEMORY_TYPES = {
    "byte": (1, _MEMORY_LIB.memoryReadByte, _MEMORY_LIB.memoryWriteByte, 0, 255),
    "int8": (1, _MEMORY_LIB.memoryReadInt8, _MEMORY_LIB.memoryWriteInt8, -(2**7), 2**7 - 1),
    "uint8": (1, _MEMORY_LIB.memoryReadUInt8, _MEMORY_LIB.memoryWriteUInt8, 0, 2**8 - 1),
    "int16": (2, _MEMORY_LIB.memoryReadInt16, _MEMORY_LIB.memoryWriteInt16, -(2**15), 2**15 - 1),
    "uint16": (2, _MEMORY_LIB.memoryReadUInt16, _MEMORY_LIB.memoryWriteUInt16, 0, 2**16 - 1),
    "int32": (4, _MEMORY_LIB.memoryReadInt32, _MEMORY_LIB.memoryWriteInt32, -(2**31), 2**31 - 1),
    "uint32": (4, _MEMORY_LIB.memoryReadUInt32, _MEMORY_LIB.memoryWriteUInt32, 0, 2**32 - 1),
    "int64": (8, _MEMORY_LIB.memoryReadInt64, _MEMORY_LIB.memoryWriteInt64, -(2**63), 2**63 - 1),
    "uint64": (8, _MEMORY_LIB.memoryReadUInt64, _MEMORY_LIB.memoryWriteUInt64, 0, 2**64 - 1),
    "float32": (4, _MEMORY_LIB.memoryReadFloat32, _MEMORY_LIB.memoryWriteFloat32, None, None),
    "float64": (8, _MEMORY_LIB.memoryReadFloat64, _MEMORY_LIB.memoryWriteFloat64, None, None),
}

_NATIVE_MODULES: dict[int, dict[str, Any]] = {}
_NATIVE_MODULE_IDS = itertools.count(1)
_PROCESSES: dict[int, "subprocess.Popen[bytes]"] = {}
_PROCESS_IDS = itertools.count(1)
_FILES: dict[int, int] = {}
_FILE_IDS = itertools.count(1)
_SOCKETS: dict[int, socket.socket] = {}
_SOCKET_IDS = itertools.count(1)
_SYNC_OBJECTS: dict[int, Any] = {}
_SYNC_IDS = itertools.count(1)
_SYNC_REGISTRY_LOCK = threading.Lock()
_ASYNC_POLLS: dict[int, Any] = {}
_ASYNC_TIMERS: dict[int, Any] = {}
_ASYNC_WAKEUPS: dict[int, Any] = {}
_ASYNC_IDS = itertools.count(1)
_ASYNC_REGISTRY_LOCK = threading.Lock()


# Handle-resolution helpers return ``(object, error)``.  Exactly one of the
# two is meaningful: when the error is None the object is valid, and when the
# error is set the caller returns immediately without touching the object.
_SyncT = TypeVar("_SyncT")


class _ManagedMutex:
    def __init__(self):
        self.lock: threading.Lock = threading.Lock()
        self.owner: int | None = None
        self.waiters: int = 0


class _ManagedCondition:
    def __init__(self):
        self.condition: threading.Condition | None = None
        self.mutex: _ManagedMutex | None = None
        self.waiters: int = 0


class _ManagedSemaphore:
    def __init__(self, value: int):
        self.semaphore: threading.Semaphore = threading.Semaphore(value)
        self.waiters: int = 0


class _AsyncPoll:
    def __init__(self):
        self.poller: select.poll = select.poll()
        self.registrations: dict[int, dict[str, Any]] = {}
        self.timers: dict[int, dict[str, Any]] = {}
        self.waiting: bool = False
        self.closed: bool = False
        self.lock: threading.RLock = threading.RLock()

    def wait(self, timeout_ms, max_events):
        with self.lock:
            if self.closed:
                raise RuntimeError("async poll is closed")
            if self.waiting:
                raise RuntimeError("async poll already has a waiting task")
            self.waiting = True
        try:
            with self.lock:
                now = time.monotonic()
                due = [
                    timer_id for timer_id, timer in self.timers.items()
                    if timer["deadline"] <= now
                ]
                if due:
                    poll_timeout = 0
                else:
                    poll_timeout = timeout_ms
                    if self.timers:
                        timer_timeout = max(
                            1,
                            int(
                                (min(timer["deadline"] for timer in self.timers.values()) - now)
                                * 1000
                            ),
                        )
                        poll_timeout = (
                            timer_timeout if poll_timeout < 0
                            else min(poll_timeout, timer_timeout)
                        )
            ready = self.poller.poll(poll_timeout)
            events = []
            with self.lock:
                for fd, mask in ready:
                    registration = self.registrations.get(fd)
                    if registration is None:
                        continue
                    if "wakeup" in registration:
                        try:
                            while os.read(registration["wakeup"].read_fd, 4096):
                                pass
                        except BlockingIOError:
                            pass
                        events.append({
                            "kind": "wakeup",
                            "fd": fd,
                            "token": registration["token"],
                        })
                        continue
                    event_names = []
                    if mask & (select.POLLIN | select.POLLPRI):
                        event_names.append("read")
                    if mask & select.POLLOUT:
                        event_names.append("write")
                    if mask & (select.POLLERR | select.POLLHUP | select.POLLNVAL):
                        event_names.append("error")
                    events.append({
                        "kind": "io",
                        "fd": fd,
                        "events": event_names,
                        "token": registration["token"],
                    })
                now = time.monotonic()
                for timer_id, timer in list(self.timers.items()):
                    if timer["deadline"] <= now:
                        events.append({
                            "kind": "timer",
                            "timer": timer_id,
                            "token": timer["token"],
                        })
                        if timer["repeat"] > 0:
                            timer["deadline"] = now + timer["repeat"]
                        else:
                            self.timers.pop(timer_id, None)
                return events[:max_events]
        finally:
            with self.lock:
                self.waiting = False

    def close(self):
        with self.lock:
            if self.waiting:
                raise RuntimeError("async poll cannot close while it is waiting")
            self.closed = True
            for fd in list(self.registrations):
                try:
                    self.poller.unregister(fd)
                except OSError:
                    pass
            self.registrations.clear()
            self.timers.clear()


class _AsyncWakeup:
    def __init__(self, poll, token):
        self.poll = poll
        self.token = token
        self.read_fd, self.write_fd = os.pipe()
        os.set_blocking(self.read_fd, False)
        os.set_blocking(self.write_fd, False)
        poll.poller.register(self.read_fd, select.POLLIN)
        poll.registrations[self.read_fd] = {
            "token": token, "wakeup": self,
        }
        self.closed = False

    def signal(self):
        if self.closed:
            raise RuntimeError("async wakeup is closed")
        try:
            os.write(self.write_fd, b"\x01")
        except BlockingIOError:
            pass

    def close(self):
        if self.closed:
            raise RuntimeError("async wakeup is already closed")
        self.poll.registrations.pop(self.read_fd, None)
        try:
            self.poll.poller.unregister(self.read_fd)
        except OSError:
            pass
        os.close(self.read_fd)
        os.close(self.write_fd)
        self.closed = True


def _close_files():
    """Close any handles a program leaves open when the runtime exits."""
    for descriptor in list(_FILES.values()):
        try:
            os.close(descriptor)
        except OSError:
            pass
    _FILES.clear()


def _close_sockets():
    for connection in list(_SOCKETS.values()):
        try:
            connection.close()
        except OSError:
            pass
    _SOCKETS.clear()


def _close_async_resources():
    for wakeup in list(_ASYNC_WAKEUPS.values()):
        try:
            wakeup.close()
        except (OSError, RuntimeError):
            pass
    _ASYNC_WAKEUPS.clear()
    for poll in list(_ASYNC_POLLS.values()):
        try:
            poll.close()
        except RuntimeError:
            pass
    _ASYNC_POLLS.clear()
    _ASYNC_TIMERS.clear()


atexit.register(_close_files)
atexit.register(_close_sockets)
atexit.register(_close_async_resources)

SYSCALL_BUILTIN_NAMES = (
    "syscallRead", "syscallWrite", "syscallOpenAt", "syscallClose",
    "syscallReadVector", "syscallWriteVector", "syscallSeekFile",
    "syscallGetFileStatus", "syscallGetFileStatusAt", "syscallTruncateFile",
    "syscallSynchronizeFile", "syscallSynchronizeFileData",
    "syscallDuplicateFileDescriptor", "syscallDuplicateFileDescriptorAt",
    "syscallCreatePipe", "syscallControlFileDescriptor",
    "syscallGetDirectoryEntries", "syscallReadSymbolicLink",
    "syscallCreateDirectoryAt", "syscallRemoveFileAt", "syscallRenameFileAt",
    "syscallCreateHardLinkAt", "syscallCreateSymbolicLinkAt",
    "syscallChangeFilePermissions", "syscallChangeFileDescriptorPermissions",
    "syscallChangeFileOwner", "syscallChangeFileDescriptorOwner",
    "syscallMemoryMap", "syscallMemoryUnmap", "syscallMemoryProtect",
    "syscallMemoryAdvise", "syscallMemoryRemap", "syscallAdjustProgramBreak",
    "syscallExecuteProgram", "syscallExecuteProgramAt", "syscallExitProcess",
    "syscallExitAllThreads", "syscallWaitForProcess", "syscallGetProcessId",
    "syscallGetParentProcessId", "syscallSendSignal", "syscallCreateThread",
    "syscallGetThreadId", "syscallWaitOnMemory", "syscallSetThreadIdAddress",
    "syscallSetRobustThreadList", "syscallGetRobustThreadList",
    "syscallYieldProcessor", "syscallGetClockTime",
    "syscallGetClockResolution", "syscallSleep", "syscallGetRandomBytes",
    "syscallCreateSocket", "syscallCreateSocketPair", "syscallBindSocket",
    "syscallListenSocket", "syscallAcceptConnection", "syscallConnectSocket",
    "syscallSendData", "syscallReceiveData", "syscallSendMessage",
    "syscallReceiveMessage", "syscallShutdownSocket",
    "syscallGetSocketAddress", "syscallGetPeerAddress",
    "syscallSetSocketOption", "syscallGetSocketOption",
    "syscallPollFileDescriptors", "syscallCreateEventPoll",
    "syscallControlEventPoll", "syscallWaitForEvents",
    "syscallGetSystemInformation", "syscallGetResourceUsage",
    "syscallGetResourceLimit", "syscallSetResourceLimit",
    "syscallControlProcess",
)


def _memory_type(value):
    return value.value.lower() if isinstance(value, String) else None


def _native_int(value):
    return (
        isinstance(value, Number)
        and not value.is_bool
        and isinstance(value.value, int)
    )


def _native_nonnegative(value):
    return _native_int(value) and value.value >= 0


def _json_value(value):
    """Convert a Lynxer value into a JSON-compatible Python value."""
    if isinstance(value, Number):
        return bool(value.value) if value.is_bool else value.value
    if isinstance(value, String):
        return value.value
    if isinstance(value, _runtime.Char):
        return value.value
    if isinstance(value, _runtime.Null):
        return None
    if isinstance(value, List):
        return [_json_value(element) for element in value.elements]
    if isinstance(value, LynxTuple):
        return [_json_value(element) for element in value.elements]
    if isinstance(value, VarGroup):
        return {
            name: _json_value(info["value"])
            for name, info in value._fields.items()
        }
    if isinstance(value, (Sentinel, ObjectValue)):
        return str(value)
    return str(value)


def _native_module_state(handle):
    state = _NATIVE_MODULES.get(handle)
    if state is None or state["closed"]:
        return None
    return state


def _load_native_module(path: str, imported: bool = False):
    """Load a native module and invoke its versioned registration entry point.

    Native modules export:
      int lynxer_module_init_v1(register_function, register_constant,
                                register_type)

    Registration callbacks receive UTF-8 names. Functions additionally provide
    an exported symbol and the existing Lynxer native-call signature grammar.
    """
    try:
        state = _MEMORY_LIB.nativeModuleLoad(os.path.abspath(path))
    except Exception as exc:
        raise RuntimeError(f"could not load native module '{path}': {exc}") from exc
    state = dict(state)
    state["closed"] = False
    state["imported"] = imported
    state["dependencies"] = _native_module_dependencies(path)
    handle = int(state["handle"])
    _NATIVE_MODULES[handle] = state
    return handle, state


def _native_module_dependencies(path: str) -> list[str]:
    """Return the shared libraries declared by a native module.

    ``ldd`` is used instead of guessing from the module filename.  Failure to
    inspect dependencies is reported as an empty list because loading has
    already succeeded and dependency inspection is informational.
    """
    if sys.platform == "win32":
        return []
    command = ["otool", "-L", path] if sys.platform == "darwin" else ["ldd", path]
    try:
        result = subprocess.run(
            command, capture_output=True, text=True, check=False, timeout=5
        )
    except (OSError, subprocess.SubprocessError):
        return []
    dependencies = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line or line.startswith(path) or line.startswith("statically"):
            continue
        if " => " in line:
            dependency = line.split(" => ", 1)[1].split(" (", 1)[0].strip()
        elif sys.platform == "darwin":
            dependency = line.split(" (", 1)[0].strip()
        else:
            continue
        if dependency and dependency != "not found" and dependency not in dependencies:
            dependencies.append(dependency)
    return dependencies


def populate_native_module_table(state, symbol_table):
    """Bind a loaded module's registered ABI surface into a Lynxer namespace."""
    for name, info in state["functions"].items():
        symbol_table.set(name, NativeModuleFunction(
            name, info["pointer"], info["signature"]
        ))
    for name, value in state["constants"].items():
        symbol_table.set(name, Number(value))
    for name, layout in state["types"].items():
        symbol_table.set(name, String(layout))


class NativeModuleFunction(BaseFunction):
    """A directly callable function registered by a native module."""

    def __init__(self, name, pointer, signature, module_handle=None):
        super().__init__(name)
        self.pointer = pointer
        self.signature = signature
        self.module_handle = module_handle

    def execute(self, args):
        res = RTResult()
        exec_ctx = self.generate_new_context()
        if self.module_handle is not None and _native_module_state(self.module_handle) is None:
            return res.failure(RTError(
                self.pos_start, self.pos_end,
                f"native module function '{self.name}' belongs to a closed module",
                exec_ctx,
            ))
        if not all(_native_int(value) for value in args):
            return res.failure(RTError(
                self.pos_start, self.pos_end,
                f"native module function '{self.name}' expects integer arguments",
                exec_ctx,
            ))
        try:
            result = _MEMORY_LIB.nativeCall(
                self.pointer, self.signature, [value.value for value in args]
            )
        except (RuntimeError, ValueError, OverflowError, MemoryError, OSError) as exc:
            return res.failure(RTError(self.pos_start, self.pos_end, str(exc), exec_ctx))
        return res.success(Number.null if result is None else Number(result))

    def copy(self):
        copied = NativeModuleFunction(
            self.name, self.pointer, self.signature, self.module_handle
        )
        copied.set_pos(self.pos_start, self.pos_end)
        copied.set_context(self.context)
        return copied

    def __repr__(self):
        return f"<native module function {self.name}>"


class BuiltInFunction(BaseFunction):
    """A callable implemented by Python and exposed to Lynxer programs."""

    def execute(self, args):
        res = RTResult()
        exec_ctx = self.generate_new_context()

        method_name = f"execute_{self.name}"
        method = getattr(self, method_name, self.no_visit_method)
        return_value = res.register(method(args, exec_ctx))

        if res.should_return():
            return res
        return res.success(return_value)

    def no_visit_method(self, node, context):
        raise Exception(f"No execute_{self.name} method defined")

    def _failure(self, exec_ctx, message):
        return RTResult().failure(
            RTError(self.pos_start, self.pos_end, message, exec_ctx)
        )

    def _cpp(self, method, values, exec_ctx):
        """Call a C++ memory primitive and translate its exception to Lynxer."""
        try:
            return method(*values)
        except (RuntimeError, ValueError, OverflowError, MemoryError, OSError) as exc:
            return self._failure(exec_ctx, str(exc))

    def copy(self):
        c = BuiltInFunction(self.name)
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        return c

    def __repr__(self):
        return f"<built-in {self.name}>"

    def execute_print(self, args, exec_ctx):
        output = "".join(str(a) for a in args)
        sys.stdout.write(output)
        sys.stdout.flush()
        return RTResult().success(Number.null)

    def execute_println(self, args, exec_ctx):
        output = "".join(str(a) for a in args)
        sys.stdout.write(output + "\n")
        sys.stdout.flush()
        return RTResult().success(Number.null)

    def execute_unshare(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "unshare() expects exactly one variable")
        # The AST-level variable name is attached by the interpreter before
        # calling this built-in; values alone are intentionally not enough to
        # identify an alias.
        name = getattr(args[0], "_lynxer_name", None)
        if not isinstance(name, str) or not exec_ctx.symbol_table.unshare(name):
            return self._failure(exec_ctx, "unshare() expects a shared variable name")
        return RTResult().success(Number.null)

    def execute_getAddress(self, args, exec_ctx):
        """Return an address pointing at a variable argument."""
        if len(args) != 1:
            return self._failure(exec_ctx, "getAddress() expects exactly one variable")
        reference = getattr(args[0], "_lynxer_ref", None)
        if reference is None:
            return self._failure(
                exec_ctx,
                "getAddress() expects a variable name, not a computed value",
            )
        table, name = reference
        pointer = table.get_reference(name)
        if pointer is None:
            return self._failure(exec_ctx, "getAddress() points to an undefined variable")
        address = Address(pointer, table, name)
        address.set_context(exec_ctx)
        return RTResult().success(address)

    def execute_getAddressValue(self, args, exec_ctx):
        """Read the value currently stored at an address."""
        if len(args) != 1 or not isinstance(args[0], Address):
            return self._failure(
                exec_ctx,
                "getAddressValue() expects exactly one address",
            )
        value = args[0].get_value()
        if value is None:
            return self._failure(exec_ctx, "getAddressValue() points to an undefined variable")
        return RTResult().success(value)

    def execute_modifyAddressValue(self, args, exec_ctx):
        """Write a value through an address while enforcing its target type."""
        if len(args) != 2 or not isinstance(args[0], Address):
            return self._failure(
                exec_ctx,
                "modifyAddressValue() expects an address and a value",
            )
        address = args[0]
        table, name = address._target()
        if table is None:
            return self._failure(exec_ctx, "modifyAddressValue() points to an undefined variable")
        if table.is_const(name):
            return self._failure(
                exec_ctx,
                f"Cannot modify constant '{name}' through an address",
            )
        declared_type = table.types.get(name)
        if not type_matches(declared_type, args[1]):
            return self._failure(
                exec_ctx,
                f"Cannot store '{value_type_name(args[1])}' in address to "
                f"'{declared_type}' variable '{name}'",
            )
        if not address.set_value(args[1]):
            return self._failure(exec_ctx, "modifyAddressValue() could not update its target")
        return RTResult().success(Number.null)

    def execute_functionAddress(self, args, exec_ctx):
        """Wrap a native pointer in a type-safe function-address value."""
        if len(args) != 1:
            return self._failure(exec_ctx, "functionAddress() expects one address")
        pointer = args[0].pointer if isinstance(args[0], FunctionAddress) else None
        if isinstance(args[0], Address):
            value = args[0].get_value()
            if not _native_nonnegative(value):
                return self._failure(
                    exec_ctx,
                    "functionAddress() expects an address containing a non-negative integer",
                )
            pointer = value.value
        elif _native_nonnegative(args[0]):
            pointer = args[0].value
        if pointer is None or pointer == 0:
            return self._failure(
                exec_ctx,
                "functionAddress() expects a non-zero integer or address",
            )
        result = FunctionAddress(pointer)
        result.set_context(exec_ctx)
        return RTResult().success(result)

    # Descriptive alias for callers that prefer the native-FFI naming.
    def execute_nativeFunctionAddress(self, args, exec_ctx):
        return self.execute_functionAddress(args, exec_ctx)

    def execute_processSpawn(self, args, exec_ctx):
        if (
            len(args) < 2
            or len(args) > 3
            or not isinstance(args[0], String)
            or not isinstance(args[1], List)
            or not all(isinstance(value, String) for value in args[1].elements)
        ):
            return self._failure(
                exec_ctx,
                "processSpawn(command, arguments, environment?) expects a command "
                "and a list of string arguments",
            )
        environment = None
        if len(args) == 3:
            if not isinstance(args[2], List) or not all(
                isinstance(value, String) and "=" in value.value
                for value in args[2].elements
            ):
                return self._failure(
                    exec_ctx,
                    "processSpawn environment must be a list of KEY=VALUE strings",
                )
            environment = os.environ.copy()
            for value in args[2].elements:
                key, item = value.value.split("=", 1)
                if not key:
                    return self._failure(
                        exec_ctx,
                        "processSpawn environment keys must not be empty",
                    )
                environment[key] = item
        try:
            process = subprocess.Popen(
                [args[0].value] + [value.value for value in args[1].elements],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=environment,
            )
        except (OSError, ValueError) as exc:
            return self._failure(exec_ctx, f"processSpawn() failed: {exc}")
        handle = next(_PROCESS_IDS)
        _PROCESSES[handle] = process
        return RTResult().success(Number(handle))

    def _process(self, value, exec_ctx, name):
        if not _native_nonnegative(value):
            return None, self._failure(exec_ctx, f"{name}() expects a process handle")
        process = _PROCESSES.get(value.value)
        if process is None:
            return None, self._failure(exec_ctx, f"{name}() received an unknown process handle")
        return process, None

    def execute_processWrite(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[1], String):
            return self._failure(exec_ctx, "processWrite(handle, data) expects a handle and string")
        process, failure = self._process(args[0], exec_ctx, "processWrite")
        if failure:
            return failure
        assert process is not None
        if process.stdin is None:
            return self._failure(exec_ctx, "processWrite() stdin is already closed")
        try:
            data = args[1].value.encode("utf-8")
            process.stdin.write(data)
            process.stdin.flush()
        except (BrokenPipeError, OSError, ValueError) as exc:
            return self._failure(exec_ctx, f"processWrite() failed: {exc}")
        return RTResult().success(Number(len(data)))

    def execute_processCloseInput(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "processCloseInput(handle) expects a process handle")
        process, failure = self._process(args[0], exec_ctx, "processCloseInput")
        if failure:
            return failure
        assert process is not None
        if process.stdin is not None:
            try:
                process.stdin.close()
            except OSError as exc:
                return self._failure(exec_ctx, f"processCloseInput() failed: {exc}")
        return RTResult().success(Number.null)

    def execute_processRead(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[1], String)
            or not _native_nonnegative(args[2])
            or args[1].value not in {"stdout", "stderr"}
        ):
            return self._failure(
                exec_ctx,
                "processRead(handle, stream, maxBytes) expects stdout/stderr and "
                "a non-negative byte count",
            )
        process, failure = self._process(args[0], exec_ctx, "processRead")
        if failure:
            return failure
        assert process is not None
        stream = process.stdout if args[1].value == "stdout" else process.stderr
        if stream is None:
            return self._failure(exec_ctx, "processRead() stream is closed")
        try:
            return RTResult().success(
                String(stream.read(args[2].value).decode("utf-8", errors="replace"))
            )
        except (OSError, ValueError) as exc:
            return self._failure(exec_ctx, f"processRead() failed: {exc}")

    def execute_processPoll(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "processPoll(handle) expects a process handle")
        process, failure = self._process(args[0], exec_ctx, "processPoll")
        if failure:
            return failure
        assert process is not None
        return RTResult().success(Number(-1 if process.poll() is None else process.returncode))

    def execute_processWait(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[1], Number)
            or args[1].is_bool
            or args[1].value < 0
        ):
            return self._failure(
                exec_ctx,
                "processWait(handle, timeoutSeconds) expects a non-negative timeout",
            )
        process, failure = self._process(args[0], exec_ctx, "processWait")
        if failure:
            return failure
        assert process is not None
        try:
            status = process.wait(timeout=float(args[1].value))
        except subprocess.TimeoutExpired:
            return RTResult().success(Number(-1))
        return RTResult().success(Number(status))

    def execute_processSendSignal(self, args, exec_ctx):
        if len(args) != 2 or not _native_nonnegative(args[1]):
            return self._failure(exec_ctx, "processSendSignal(handle, signal) expects a signal number")
        process, failure = self._process(args[0], exec_ctx, "processSendSignal")
        if failure:
            return failure
        assert process is not None
        if process.poll() is not None:
            return self._failure(exec_ctx, "processSendSignal() process has already exited")
        try:
            process.send_signal(args[1].value)
        except (OSError, ValueError) as exc:
            return self._failure(exec_ctx, f"processSendSignal() failed: {exc}")
        return RTResult().success(Number.null)

    def execute_processClose(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "processClose(handle) expects a process handle")
        process, failure = self._process(args[0], exec_ctx, "processClose")
        if failure:
            return failure
        assert process is not None
        for stream in (process.stdin, process.stdout, process.stderr):
            if stream is not None and not stream.closed:
                stream.close()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=0.2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        _PROCESSES.pop(args[0].value, None)
        return RTResult().success(Number.null)

    # The filesystem* API is intentionally small and handle-based.  It keeps the
    # low-level syscall builtins available while giving Lynxer programs one
    # consistent, errno-preserving filesystem surface.
    def execute_filesystemOpen(self, args, exec_ctx):
        if (
            len(args) not in {2, 3}
            or not isinstance(args[0], String)
            or not isinstance(args[1], String)
            or (len(args) == 3 and not _native_nonnegative(args[2]))
        ):
            return self._failure(exec_ctx, "filesystemOpen(path, mode, permissions?) expects strings and an optional integer")
        flags_by_mode = {
            "r": os.O_RDONLY,
            "w": os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
            "a": os.O_WRONLY | os.O_CREAT | os.O_APPEND,
            "r+": os.O_RDWR,
            "w+": os.O_RDWR | os.O_CREAT | os.O_TRUNC,
            "a+": os.O_RDWR | os.O_CREAT | os.O_APPEND,
        }
        mode = args[1].value
        flags = flags_by_mode.get(mode)
        if flags is None:
            return self._failure(exec_ctx, "filesystemOpen() mode must be r, w, a, r+, w+, or a+")
        permissions = args[2].value if len(args) == 3 else 0o666
        try:
            descriptor = os.open(args[0].value, flags, permissions)
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemOpen() failed: [{exc.errno}] {exc.strerror}")
        handle = next(_FILE_IDS)
        _FILES[handle] = descriptor
        return RTResult().success(Number(handle))

    def _file(self, value, exec_ctx, name):
        if not _native_nonnegative(value):
            return None, self._failure(exec_ctx, f"{name}() expects a file handle")
        descriptor = _FILES.get(value.value)
        if descriptor is None:
            return None, self._failure(exec_ctx, f"{name}() received an unknown or closed file handle")
        return descriptor, None

    def execute_filesystemRead(self, args, exec_ctx):
        if len(args) != 2 or not _native_nonnegative(args[1]):
            return self._failure(exec_ctx, "filesystemRead(handle, maxBytes) expects a non-negative byte count")
        descriptor, failure = self._file(args[0], exec_ctx, "filesystemRead")
        if failure:
            return failure
        assert descriptor is not None
        try:
            return RTResult().success(String(os.read(descriptor, args[1].value).decode("utf-8", errors="replace")))
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemRead() failed: [{exc.errno}] {exc.strerror}")

    def execute_filesystemWrite(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[1], String):
            return self._failure(exec_ctx, "filesystemWrite(handle, data) expects a file handle and string")
        descriptor, failure = self._file(args[0], exec_ctx, "filesystemWrite")
        if failure:
            return failure
        assert descriptor is not None
        try:
            return RTResult().success(Number(os.write(descriptor, args[1].value.encode("utf-8"))))
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemWrite() failed: [{exc.errno}] {exc.strerror}")

    def execute_filesystemClose(self, args, exec_ctx):
        descriptor, failure = self._file(args[0], exec_ctx, "filesystemClose") if len(args) == 1 else (None, self._failure(exec_ctx, "filesystemClose(handle) expects a file handle"))
        if failure:
            return failure
        assert descriptor is not None
        try:
            os.close(descriptor)
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemClose() failed: [{exc.errno}] {exc.strerror}")
        _FILES.pop(args[0].value, None)
        return RTResult().success(Number.null)

    def execute_filesystemStat(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "filesystemStat(path) expects a path string")
        try:
            info = os.lstat(args[0].value)
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemStat() failed: [{exc.errno}] {exc.strerror}")
        kind = "symlink" if stat.S_ISLNK(info.st_mode) else "file" if stat.S_ISREG(info.st_mode) else "dir" if stat.S_ISDIR(info.st_mode) else "other"
        return RTResult().success(String(json.dumps({
            "type": kind, "size": info.st_size, "mode": stat.S_IMODE(info.st_mode),
            "modifiedTime": info.st_mtime, "accessTime": info.st_atime, "changeTime": info.st_ctime,
        }, separators=(",", ":"))))

    def execute_filesystemList(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "filesystemList(path) expects a directory path string")
        try:
            entries = sorted(os.listdir(args[0].value))
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemList() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(List([String(entry) for entry in entries]))

    def execute_filesystemMkdir(self, args, exec_ctx):
        if len(args) not in {1, 2} or not isinstance(args[0], String) or (len(args) == 2 and not isinstance(args[1], Number)):
            return self._failure(exec_ctx, "filesystemMkdir(path, parents?) expects a path and optional boolean")
        try:
            if len(args) == 2 and args[1].is_true():
                os.makedirs(args[0].value, exist_ok=True)
            else:
                os.mkdir(args[0].value)
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemMkdir() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number(1, is_bool=True))

    def execute_filesystemRemove(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "filesystemRemove(path) expects a path string")
        try:
            os.rmdir(args[0].value) if os.path.isdir(args[0].value) and not os.path.islink(args[0].value) else os.unlink(args[0].value)
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemRemove() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number.null)

    def execute_filesystemRename(self, args, exec_ctx):
        if len(args) != 2 or not all(isinstance(value, String) for value in args):
            return self._failure(exec_ctx, "filesystemRename(source, target) expects two path strings")
        try:
            os.rename(args[0].value, args[1].value)
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemRename() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number.null)

    def execute_filesystemLink(self, args, exec_ctx):
        if len(args) not in {2, 3} or not all(isinstance(value, String) for value in args[:2]) or (len(args) == 3 and not isinstance(args[2], Number)):
            return self._failure(exec_ctx, "filesystemLink(source, target, symbolic?) expects paths and an optional boolean")
        try:
            if len(args) == 3 and args[2].is_true():
                os.symlink(args[0].value, args[1].value)
            else:
                os.link(args[0].value, args[1].value)
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemLink() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number.null)

    def execute_filesystemReadLink(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "filesystemReadLink(path) expects a path string")
        try:
            return RTResult().success(String(os.readlink(args[0].value)))
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemReadLink() failed: [{exc.errno}] {exc.strerror}")

    def execute_filesystemChmod(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], String) or not _native_nonnegative(args[1]):
            return self._failure(exec_ctx, "filesystemChmod(path, mode) expects a path and non-negative integer mode")
        try:
            os.chmod(args[0].value, args[1].value)
        except OSError as exc:
            return self._failure(exec_ctx, f"filesystemChmod() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number.null)

    # Managed TCP, UDP, and Unix-domain sockets. Addresses are deliberately
    # represented by host/path strings plus an integer port so the API stays
    # straightforward in Lynxer source.
    def execute_networkingOpen(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "networkingOpen(kind) expects tcp, udp, or unix")
        families = {"tcp": (socket.AF_INET, socket.SOCK_STREAM), "udp": (socket.AF_INET, socket.SOCK_DGRAM), "unix": (socket.AF_UNIX, socket.SOCK_STREAM)}
        kind = args[0].value.lower()
        family_type = families.get(kind)
        if family_type is None:
            return self._failure(exec_ctx, "networkingOpen() kind must be tcp, udp, or unix")
        try:
            connection = socket.socket(*family_type)
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingOpen() failed: [{exc.errno}] {exc.strerror}")
        handle = next(_SOCKET_IDS)
        _SOCKETS[handle] = connection
        return RTResult().success(Number(handle))

    def _socket(self, value, exec_ctx, name):
        if not _native_nonnegative(value):
            return None, self._failure(exec_ctx, f"{name}() expects a socket handle")
        connection = _SOCKETS.get(value.value)
        if connection is None:
            return None, self._failure(exec_ctx, f"{name}() received an unknown or closed socket handle")
        return connection, None

    def _socket_address(self, args, exec_ctx, name):
        if len(args) == 2 and isinstance(args[1], String):
            return args[1].value, None
        if len(args) == 3 and isinstance(args[1], String) and _native_nonnegative(args[2]):
            return (args[1].value, args[2].value), None
        return None, self._failure(exec_ctx, f"{name}(handle, address, port?) expects an address and optional non-negative port")

    def execute_networkingBind(self, args, exec_ctx):
        connection, failure = self._socket(args[0], exec_ctx, "networkingBind") if args else (None, self._failure(exec_ctx, "networkingBind(handle, address, port?) expects a socket handle"))
        if failure:
            return failure
        assert connection is not None
        address, failure = self._socket_address(args, exec_ctx, "networkingBind")
        if failure:
            return failure
        assert address is not None
        try:
            connection.bind(address)
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingBind() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number.null)

    def execute_networkingListen(self, args, exec_ctx):
        if len(args) not in {1, 2} or (len(args) == 2 and not _native_nonnegative(args[1])):
            return self._failure(exec_ctx, "networkingListen(handle, backlog?) expects a socket handle and optional integer")
        connection, failure = self._socket(args[0], exec_ctx, "networkingListen")
        if failure:
            return failure
        assert connection is not None
        try:
            connection.listen(args[1].value if len(args) == 2 else 128)
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingListen() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number.null)

    def execute_networkingAccept(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "networkingAccept(handle) expects a socket handle")
        connection, failure = self._socket(args[0], exec_ctx, "networkingAccept")
        if failure:
            return failure
        assert connection is not None
        try:
            accepted, _address = connection.accept()
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingAccept() failed: [{exc.errno}] {exc.strerror}")
        handle = next(_SOCKET_IDS)
        _SOCKETS[handle] = accepted
        return RTResult().success(Number(handle))

    def execute_networkingConnect(self, args, exec_ctx):
        connection, failure = self._socket(args[0], exec_ctx, "networkingConnect") if args else (None, self._failure(exec_ctx, "networkingConnect(handle, address, port?) expects a socket handle"))
        if failure:
            return failure
        assert connection is not None
        address, failure = self._socket_address(args, exec_ctx, "networkingConnect")
        if failure:
            return failure
        assert address is not None
        try:
            connection.connect(address)
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingConnect() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number.null)

    def execute_networkingSend(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[1], String):
            return self._failure(exec_ctx, "networkingSend(handle, data) expects a socket handle and string")
        connection, failure = self._socket(args[0], exec_ctx, "networkingSend")
        if failure:
            return failure
        assert connection is not None
        try:
            return RTResult().success(Number(connection.send(args[1].value.encode("utf-8"))))
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingSend() failed: [{exc.errno}] {exc.strerror}")

    def execute_networkingReceive(self, args, exec_ctx):
        if len(args) != 2 or not _native_nonnegative(args[1]):
            return self._failure(exec_ctx, "networkingReceive(handle, maxBytes) expects a non-negative byte count")
        connection, failure = self._socket(args[0], exec_ctx, "networkingReceive")
        if failure:
            return failure
        assert connection is not None
        try:
            return RTResult().success(String(connection.recv(args[1].value).decode("utf-8", errors="replace")))
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingReceive() failed: [{exc.errno}] {exc.strerror}")

    def execute_networkingClose(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "networkingClose(handle) expects a socket handle")
        connection, failure = self._socket(args[0], exec_ctx, "networkingClose")
        if failure:
            return failure
        assert connection is not None
        try:
            connection.close()
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingClose() failed: [{exc.errno}] {exc.strerror}")
        _SOCKETS.pop(args[0].value, None)
        return RTResult().success(Number.null)

    def execute_networkingShutdown(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[1], String) or args[1].value not in {"read", "write", "both"}:
            return self._failure(exec_ctx, "networkingShutdown(handle, how) expects read, write, or both")
        connection, failure = self._socket(args[0], exec_ctx, "networkingShutdown")
        if failure:
            return failure
        assert connection is not None
        try:
            connection.shutdown({"read": socket.SHUT_RD, "write": socket.SHUT_WR, "both": socket.SHUT_RDWR}[args[1].value])
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingShutdown() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number.null)

    def execute_networkingBlocking(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[1], Number):
            return self._failure(exec_ctx, "networkingBlocking(handle, enabled) expects a socket handle and boolean")
        connection, failure = self._socket(args[0], exec_ctx, "networkingBlocking")
        if failure:
            return failure
        assert connection is not None
        try:
            connection.setblocking(args[1].is_true())
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingBlocking() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number.null)

    def execute_networkingOption(self, args, exec_ctx):
        if len(args) != 3 or not isinstance(args[1], String) or not isinstance(args[2], Number):
            return self._failure(exec_ctx, "networkingOption(handle, name, value) expects a socket handle, name, and integer")
        options = {"reuseAddr": socket.SO_REUSEADDR, "keepAlive": socket.SO_KEEPALIVE, "broadcast": socket.SO_BROADCAST}
        option = options.get(args[1].value)
        if option is None:
            return self._failure(exec_ctx, "networkingOption() supports reuseAddr, keepAlive, and broadcast")
        connection, failure = self._socket(args[0], exec_ctx, "networkingOption")
        if failure:
            return failure
        assert connection is not None
        try:
            connection.setsockopt(socket.SOL_SOCKET, option, int(args[2].value))
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingOption() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(Number.null)

    def execute_networkingResolve(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], String) or not _native_nonnegative(args[1]):
            return self._failure(exec_ctx, "networkingResolve(host, port) expects a host and non-negative port")
        try:
            addresses = sorted({item[4][0] for item in socket.getaddrinfo(args[0].value, args[1].value, type=socket.SOCK_STREAM)})
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingResolve() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(List([String(address) for address in addresses]))

    def execute_networkingAddress(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "networkingAddress(handle) expects a socket handle")
        connection, failure = self._socket(args[0], exec_ctx, "networkingAddress")
        if failure:
            return failure
        assert connection is not None
        try:
            address = connection.getsockname()
        except OSError as exc:
            return self._failure(exec_ctx, f"networkingAddress() failed: [{exc.errno}] {exc.strerror}")
        return RTResult().success(String(json.dumps(address, separators=(",", ":"))))

    def execute_nativeHandleAllocate(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "nativeHandleAllocate(size) expects a non-negative integer")
        result = self._cpp(_MEMORY_LIB.memoryAllocate, [args[0].value], exec_ctx)
        if isinstance(result, RTResult):
            return result
        if result == 0:
            return self._failure(exec_ctx, "nativeHandleAllocate() could not allocate memory")
        handle = NativeHandle(result)
        handle.set_context(exec_ctx)
        return RTResult().success(handle)

    def _native_handle_pointer(self, value, exec_ctx, name):
        if not isinstance(value, NativeHandle):
            return None, self._failure(exec_ctx, f"{name}() expects a nativeHandle")
        if not value.active:
            return None, self._failure(exec_ctx, f"{name}() cannot use a freed nativeHandle")
        return value.pointer, None

    def execute_nativeHandleAddress(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "nativeHandleAddress(handle) expects a nativeHandle")
        pointer, failure = self._native_handle_pointer(args[0], exec_ctx, "nativeHandleAddress")
        return failure or RTResult().success(Number(pointer))

    def execute_nativeHandleFree(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "nativeHandleFree(handle) expects a nativeHandle")
        handle = args[0]
        pointer, failure = self._native_handle_pointer(handle, exec_ctx, "nativeHandleFree")
        if failure:
            return failure
        result = self._cpp(_MEMORY_LIB.memoryFree, [pointer], exec_ctx)
        if isinstance(result, RTResult):
            return result
        handle._state["active"] = False
        return RTResult().success(Number.null)

    def execute_nativeHandleIsAlive(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], NativeHandle):
            return self._failure(exec_ctx, "nativeHandleIsAlive(handle) expects a nativeHandle")
        return RTResult().success(Number(int(args[0].active), is_bool=True))

    def execute_atomicLoad(self, args, exec_ctx):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not isinstance(args[2], String)
        ):
            return self._failure(exec_ctx, "atomicLoad(address, offset, type) expects an address, offset, and integer type")
        result = self._cpp(_MEMORY_LIB.atomicLoad, [args[0].value, args[1].value, args[2].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_atomicStore(self, args, exec_ctx):
        if (
            len(args) != 4
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not isinstance(args[2], String)
            or not _native_int(args[3])
        ):
            return self._failure(exec_ctx, "atomicStore(address, offset, type, value) expects an address, offset, integer type, and integer")
        result = self._cpp(
            _MEMORY_LIB.atomicStore,
            [args[0].value, args[1].value, args[2].value, args[3].value],
            exec_ctx,
        )
        return result if isinstance(result, RTResult) else RTResult().success(Number.null)

    def execute_atomicAdd(self, args, exec_ctx):
        if (
            len(args) != 4
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not isinstance(args[2], String)
            or not _native_int(args[3])
        ):
            return self._failure(exec_ctx, "atomicAdd(address, offset, type, value) expects an address, offset, integer type, and integer")
        result = self._cpp(
            _MEMORY_LIB.atomicAdd,
            [args[0].value, args[1].value, args[2].value, args[3].value],
            exec_ctx,
        )
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_volatileRead(self, args, exec_ctx):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not isinstance(args[2], String)
        ):
            return self._failure(exec_ctx, "volatileRead(address, offset, type) expects an address, offset, and type")
        result = self._cpp(_MEMORY_LIB.volatileRead, [args[0].value, args[1].value, args[2].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_volatileWrite(self, args, exec_ctx):
        if (
            len(args) != 4
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not isinstance(args[2], String)
            or not _native_nonnegative(args[3])
        ):
            return self._failure(exec_ctx, "volatileWrite(address, offset, type, value) expects an address, offset, type, and non-negative integer")
        result = self._cpp(
            _MEMORY_LIB.volatileWrite,
            [args[0].value, args[1].value, args[2].value, args[3].value],
            exec_ctx,
        )
        return result if isinstance(result, RTResult) else RTResult().success(Number.null)

    def execute_memoryProtect(self, args, exec_ctx):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not isinstance(args[2], String)
        ):
            return self._failure(exec_ctx, "memoryProtect(address, size, mode) expects an address, size, and protection mode")
        result = self._cpp(
            _MEMORY_LIB.memoryProtect,
            [args[0].value, args[1].value, args[2].value],
            exec_ctx,
        )
        return result if isinstance(result, RTResult) else RTResult().success(Number.null)

    def execute_nativeCall(self, args, exec_ctx):
        pointer = None
        if len(args) == 3:
            if isinstance(args[0], FunctionAddress):
                pointer = args[0].pointer
            elif isinstance(args[0], Address):
                value = args[0].get_value()
                if _native_nonnegative(value):
                    pointer = value.value
            elif _native_nonnegative(args[0]):
                pointer = args[0].value
        if (
            len(args) != 3
            or pointer is None
            or pointer == 0
            or not isinstance(args[1], String)
            or not isinstance(args[2], List)
        ):
            return self._failure(
                exec_ctx,
                "nativeCall(address, signature, arguments) expects a non-zero "
                "address (integer, address value, or functionAddress)",
            )
        native_args = []
        for value in args[2].elements:
            if not _native_int(value):
                return self._failure(
                    exec_ctx,
                    "nativeCall arguments must be integers",
                )
            native_args.append(value.value)
        result = self._cpp(
            _MEMORY_LIB.nativeCall,
            [pointer, args[1].value, native_args],
            exec_ctx,
        )
        if isinstance(result, RTResult):
            return result
        if result is None:
            return RTResult().success(Number.null)
        return RTResult().success(Number(result))

    def execute_ffiLoadLibrary(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "ffiLoadLibrary(path) expects a library path")
        try:
            handle = _MEMORY_LIB.ffiLoadLibrary(args[0].value)
        except Exception as exc:
            return self._failure(exec_ctx, f"ffiLoadLibrary() failed: {exc}")
        return RTResult().success(Number(handle))

    def execute_ffiLookup(self, args, exec_ctx):
        if len(args) != 2 or not _native_nonnegative(args[0]) or not isinstance(args[1], String):
            return self._failure(exec_ctx, "ffiLookup(library, symbol) expects a library handle and symbol")
        try:
            pointer = _MEMORY_LIB.ffiLookup(args[0].value, args[1].value)
        except Exception as exc:
            return self._failure(exec_ctx, f"ffiLookup() failed: {exc}")
        result = FunctionAddress(pointer)
        result.set_context(exec_ctx)
        return RTResult().success(result)

    def execute_ffiCloseLibrary(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "ffiCloseLibrary(library) expects a library handle")
        try:
            _MEMORY_LIB.ffiCloseLibrary(args[0].value)
        except Exception as exc:
            return self._failure(exec_ctx, f"ffiCloseLibrary() failed: {exc}")
        return RTResult().success(Number.null)

    # Clear, flat names for the low-level library API.  Keep ffi* above as
    # source-compatible aliases for existing programs.
    def execute_nativeLibraryLoad(self, args, exec_ctx):
        return self.execute_ffiLoadLibrary(args, exec_ctx)

    def execute_nativeLibraryLookup(self, args, exec_ctx):
        return self.execute_ffiLookup(args, exec_ctx)

    def execute_nativeLibraryClose(self, args, exec_ctx):
        return self.execute_ffiCloseLibrary(args, exec_ctx)

    def execute_nativeFunctionCall(self, args, exec_ctx):
        return self.execute_ffiCall(args, exec_ctx)

    def execute_nativeFunctionCallback(self, args, exec_ctx):
        return self.execute_ffiCallback(args, exec_ctx)

    def execute_nativeFunctionFreeCallback(self, args, exec_ctx):
        return self.execute_ffiFreeCallback(args, exec_ctx)

    def execute_nativeModuleLoad(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "nativeModuleLoad(path) expects a library path")
        try:
            handle, _ = _load_native_module(args[0].value)
        except RuntimeError as exc:
            return self._failure(exec_ctx, str(exc))
        return RTResult().success(Number(handle))

    def execute_nativeModuleName(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "nativeModuleName(module) expects a module handle")
        state = _native_module_state(args[0].value)
        if state is None:
            return self._failure(exec_ctx, "nativeModuleName() received an unknown module handle")
        return RTResult().success(String(state["name"]))

    def execute_nativeModuleFunction(self, args, exec_ctx):
        if (
            len(args) != 2
            or not _native_nonnegative(args[0])
            or not isinstance(args[1], String)
        ):
            return self._failure(
                exec_ctx,
                "nativeModuleFunction(module, name) expects a module handle and function name",
            )
        state = _native_module_state(args[0].value)
        if state is None:
            return self._failure(exec_ctx, "nativeModuleFunction() received an unknown module handle")
        function = state["functions"].get(args[1].value)
        if function is None:
            return self._failure(
                exec_ctx, f"native module function '{args[1].value}' is not registered"
            )
        result = FunctionAddress(function["pointer"], args[0].value)
        result.set_context(exec_ctx)
        return RTResult().success(result)

    def execute_nativeModuleConstant(self, args, exec_ctx):
        if (
            len(args) != 2
            or not _native_nonnegative(args[0])
            or not isinstance(args[1], String)
        ):
            return self._failure(
                exec_ctx,
                "nativeModuleConstant(module, name) expects a module handle and constant name",
            )
        state = _native_module_state(args[0].value)
        if state is None:
            return self._failure(exec_ctx, "nativeModuleConstant() received an unknown module handle")
        if args[1].value not in state["constants"]:
            return self._failure(
                exec_ctx, f"native module constant '{args[1].value}' is not registered"
            )
        return RTResult().success(Number(state["constants"][args[1].value]))

    def execute_nativeModuleType(self, args, exec_ctx):
        if (
            len(args) != 2
            or not _native_nonnegative(args[0])
            or not isinstance(args[1], String)
        ):
            return self._failure(
                exec_ctx,
                "nativeModuleType(module, name) expects a module handle and type name",
            )
        state = _native_module_state(args[0].value)
        if state is None:
            return self._failure(exec_ctx, "nativeModuleType() received an unknown module handle")
        layout = state["types"].get(args[1].value)
        if layout is None:
            return self._failure(
                exec_ctx, f"native module type '{args[1].value}' is not registered"
            )
        return RTResult().success(String(layout))

    def execute_nativeModuleError(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "nativeModuleError(module) expects a module handle")
        state = _native_module_state(args[0].value)
        if state is None:
            return self._failure(exec_ctx, "nativeModuleError() received an unknown module handle")
        return RTResult().success(String(state.get("error", "")))

    def execute_nativeModuleDependencies(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(
                exec_ctx, "nativeModuleDependencies(module) expects a module handle"
            )
        state = _native_module_state(args[0].value)
        if state is None:
            return self._failure(
                exec_ctx, "nativeModuleDependencies() received an unknown module handle"
            )
        return RTResult().success(List([String(path) for path in state["dependencies"]]))

    def execute_nativeModuleClose(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "nativeModuleClose(module) expects a module handle")
        state = _native_module_state(args[0].value)
        if state is None:
            return self._failure(exec_ctx, "nativeModuleClose() received an unknown module handle")
        if state["imported"]:
            return self._failure(
                exec_ctx,
                "nativeModuleClose() cannot close a module imported into a namespace",
            )
        try:
            _MEMORY_LIB.nativeModuleClose(args[0].value)
        except Exception as exc:
            return self._failure(exec_ctx, f"nativeModuleClose() failed: {exc}")
        state["closed"] = True
        _NATIVE_MODULES.pop(args[0].value, None)
        return RTResult().success(Number.null)

    def execute_ffiCall(self, args, exec_ctx):
        if len(args) != 3 or not isinstance(args[1], String) or not isinstance(args[2], List):
            return self._failure(exec_ctx, "ffiCall(address, signature, arguments) expects an address, signature, and list")
        if (
            isinstance(args[0], FunctionAddress)
            and args[0].module_handle is not None
            and _native_module_state(args[0].module_handle) is None
        ):
            return self._failure(exec_ctx, "ffiCall() cannot call a function from a closed native module")
        pointer = args[0].pointer if isinstance(args[0], FunctionAddress) else (
            args[0].value if _native_nonnegative(args[0]) else None
        )
        if not pointer:
            return self._failure(exec_ctx, "ffiCall() expects a non-zero function address")
        try:
            values = [
                value.value if isinstance(value, (Number, String)) else
                (_ for _ in ()).throw(TypeError(
                    "FFI arguments must be integers, numbers, or strings"
                ))
                for value in args[2].elements
            ]
            raw = _MEMORY_LIB.ffiCall(pointer, args[1].value, values)
            result_name = args[1].value.split("(", 1)[0].split(":")[-1].strip()
            result = Number.null if result_name == "void" else (
                String(raw) if result_name == "cstring" else Number(raw)
            )
        except (TypeError, ValueError, OSError) as exc:
            return self._failure(exec_ctx, f"ffiCall() failed: {exc}")
        return RTResult().success(result)

    def execute_ffiCallback(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], String) or not hasattr(args[1], "execute"):
            return self._failure(exec_ctx, "ffiCallback(signature, function) expects a signature and Lynxer function")
        try:
            pointer = _MEMORY_LIB.ffiCallback(args[0].value, args[1])
        except (TypeError, ValueError, OSError) as exc:
            return self._failure(exec_ctx, f"ffiCallback() failed: {exc}")
        result = FunctionAddress(pointer)
        result.set_context(exec_ctx)
        return RTResult().success(result)

    def execute_ffiFreeCallback(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], FunctionAddress):
            return self._failure(exec_ctx, "ffiFreeCallback(callback) expects a function address")
        try:
            _MEMORY_LIB.ffiFreeCallback(args[0].pointer)
        except Exception as exc:
            return self._failure(exec_ctx, f"ffiFreeCallback() failed: {exc}")
        return RTResult().success(Number.null)

    def execute_nativeThreadStart(self, args, exec_ctx):
        if len(args) != 2 or not hasattr(args[0], "execute") or not isinstance(args[1], List):
            return self._failure(exec_ctx, "nativeThreadStart(function, arguments) expects a function and list")
        result = self._cpp(_MEMORY_LIB.nativeThreadStart, [args[0], args[1].elements], exec_ctx)
        if isinstance(result, RTResult):
            return result
        return RTResult().success(Number(result))

    def execute_nativeThreadJoin(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "nativeThreadJoin(handle) expects a thread handle")
        result = self._cpp(_MEMORY_LIB.nativeThreadJoin, [args[0].value], exec_ctx)
        if isinstance(result, RTResult):
            return result
        return RTResult().success(String(result))

    def execute_nativeThreadIsAlive(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "nativeThreadIsAlive(handle) expects a thread handle")
        result = self._cpp(_MEMORY_LIB.nativeThreadIsAlive, [args[0].value], exec_ctx)
        if isinstance(result, RTResult):
            return result
        return RTResult().success(Number(1 if result else 0, is_bool=True))

    def execute_nativeThreadStatus(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "nativeThreadStatus(handle) expects a thread handle")
        result = self._cpp(_MEMORY_LIB.nativeThreadStatus, [args[0].value], exec_ctx)
        if isinstance(result, RTResult):
            return result
        return RTResult().success(String(result))

    def execute_nativeThreadDetach(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "nativeThreadDetach(handle) expects a thread handle")
        result = self._cpp(_MEMORY_LIB.nativeThreadDetach, [args[0].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number.null)

    def _sync_handle(
        self, args: list[Any], exec_ctx: Any, name: str, kind: type[_SyncT]
    ) -> "tuple[_SyncT, RTResult | None]":
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return cast(_SyncT, None), self._failure(
                exec_ctx, f"{name}(handle) expects a valid handle"
            )
        handle = args[0].value
        with _SYNC_REGISTRY_LOCK:
            value = _SYNC_OBJECTS.get(handle)
        if value is None or not isinstance(value, kind):
            return cast(_SyncT, None), self._failure(
                exec_ctx, f"{name}() received an unknown or closed handle"
            )
        return value, None

    def execute_nativeMutexCreate(self, args, exec_ctx):
        if args:
            return self._failure(exec_ctx, "nativeMutexCreate() expects no arguments")
        handle = next(_SYNC_IDS)
        with _SYNC_REGISTRY_LOCK:
            _SYNC_OBJECTS[handle] = _ManagedMutex()
        return RTResult().success(Number(handle))

    def execute_nativeMutexLock(self, args, exec_ctx):
        mutex, error = self._sync_handle(args, exec_ctx, "nativeMutexLock", _ManagedMutex)
        if error:
            return error
        thread_id = threading.get_ident()
        with _SYNC_REGISTRY_LOCK:
            if mutex.owner == thread_id:
                return self._failure(exec_ctx, "nativeMutexLock() is not recursive")
            mutex.waiters += 1
        try:
            mutex.lock.acquire()
        finally:
            with _SYNC_REGISTRY_LOCK:
                mutex.waiters -= 1
        with _SYNC_REGISTRY_LOCK:
            mutex.owner = thread_id
        return RTResult().success(Number.null)

    def execute_nativeMutexTryLock(self, args, exec_ctx):
        mutex, error = self._sync_handle(args, exec_ctx, "nativeMutexTryLock", _ManagedMutex)
        if error:
            return error
        thread_id = threading.get_ident()
        with _SYNC_REGISTRY_LOCK:
            if mutex.owner == thread_id:
                return RTResult().success(Number(0, is_bool=True))
        if not mutex.lock.acquire(False):
            return RTResult().success(Number(0, is_bool=True))
        with _SYNC_REGISTRY_LOCK:
            mutex.owner = thread_id
        return RTResult().success(Number(1, is_bool=True))

    def execute_nativeMutexUnlock(self, args, exec_ctx):
        mutex, error = self._sync_handle(args, exec_ctx, "nativeMutexUnlock", _ManagedMutex)
        if error:
            return error
        with _SYNC_REGISTRY_LOCK:
            if mutex.owner != threading.get_ident():
                return self._failure(exec_ctx, "nativeMutexUnlock() requires the owning thread")
            mutex.owner = None
        mutex.lock.release()
        return RTResult().success(Number.null)

    def execute_nativeMutexClose(self, args, exec_ctx):
        mutex, error = self._sync_handle(args, exec_ctx, "nativeMutexClose", _ManagedMutex)
        if error:
            return error
        with _SYNC_REGISTRY_LOCK:
            if mutex.owner is not None or mutex.waiters:
                return self._failure(exec_ctx, "nativeMutexClose() cannot close a locked or awaited mutex")
            for handle, value in _SYNC_OBJECTS.items():
                if value is mutex:
                    del _SYNC_OBJECTS[handle]
                    break
        return RTResult().success(Number.null)

    def execute_nativeConditionCreate(self, args, exec_ctx):
        if args:
            return self._failure(exec_ctx, "nativeConditionCreate() expects no arguments")
        handle = next(_SYNC_IDS)
        condition = _ManagedCondition()
        with _SYNC_REGISTRY_LOCK:
            _SYNC_OBJECTS[handle] = condition
        return RTResult().success(Number(handle))

    def _condition_mutex(
        self, args: list[Any], exec_ctx: Any, name: str
    ) -> "tuple[_ManagedCondition, _ManagedMutex, RTResult | None]":
        if len(args) != 2:
            return (
                cast(_ManagedCondition, None),
                cast(_ManagedMutex, None),
                self._failure(exec_ctx, f"{name}(condition, mutex) expects two handles"),
            )
        condition, error = self._sync_handle(args[:1], exec_ctx, name, _ManagedCondition)
        if error:
            return cast(_ManagedCondition, None), cast(_ManagedMutex, None), error
        mutex, error = self._sync_handle(args[1:], exec_ctx, name, _ManagedMutex)
        if error:
            return cast(_ManagedCondition, None), cast(_ManagedMutex, None), error
        if mutex.owner != threading.get_ident():
            return (
                cast(_ManagedCondition, None),
                cast(_ManagedMutex, None),
                self._failure(
                    exec_ctx, f"{name}() requires the owning thread to hold the mutex"
                ),
            )
        if condition.condition is None:
            condition.condition = threading.Condition(mutex.lock)
            condition.mutex = mutex
        elif condition.mutex is not mutex:
            return (
                cast(_ManagedCondition, None),
                cast(_ManagedMutex, None),
                self._failure(
                    exec_ctx, f"{name}() condition is bound to a different mutex"
                ),
            )
        return condition, mutex, None

    def execute_nativeConditionWait(self, args, exec_ctx):
        condition, mutex, error = self._condition_mutex(args, exec_ctx, "nativeConditionWait")
        if error:
            return error
        # _condition_mutex() binds the condition to the mutex before returning,
        # so the underlying threading.Condition always exists here.
        cond = condition.condition
        assert cond is not None
        with _SYNC_REGISTRY_LOCK:
            condition.waiters += 1
            mutex.owner = None
        try:
            cond.wait()
        finally:
            with _SYNC_REGISTRY_LOCK:
                condition.waiters -= 1
                mutex.owner = threading.get_ident()
        return RTResult().success(Number.null)

    def _condition_notify(self, args, exec_ctx, all_waiters):
        name = "nativeConditionNotifyAll" if all_waiters else "nativeConditionNotify"
        condition, mutex, error = self._condition_mutex(args, exec_ctx, name)
        if error:
            return error
        # _condition_mutex() binds the condition to the mutex before returning,
        # so the underlying threading.Condition always exists here.
        cond = condition.condition
        assert cond is not None
        if all_waiters:
            cond.notify_all()
        else:
            cond.notify()
        return RTResult().success(Number.null)

    def execute_nativeConditionNotify(self, args, exec_ctx):
        return self._condition_notify(args, exec_ctx, False)

    def execute_nativeConditionNotifyAll(self, args, exec_ctx):
        return self._condition_notify(args, exec_ctx, True)

    def execute_nativeConditionClose(self, args, exec_ctx):
        condition, error = self._sync_handle(
            args, exec_ctx, "nativeConditionClose", _ManagedCondition
        )
        if error:
            return error
        with _SYNC_REGISTRY_LOCK:
            if condition.waiters:
                return self._failure(exec_ctx, "nativeConditionClose() cannot close a waiting condition")
            for handle, value in _SYNC_OBJECTS.items():
                if value is condition:
                    del _SYNC_OBJECTS[handle]
                    break
        return RTResult().success(Number.null)

    def execute_nativeSemaphoreCreate(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "nativeSemaphoreCreate(initial) expects a nonnegative integer")
        handle = next(_SYNC_IDS)
        with _SYNC_REGISTRY_LOCK:
            _SYNC_OBJECTS[handle] = _ManagedSemaphore(args[0].value)
        return RTResult().success(Number(handle))

    def _semaphore(self, args, exec_ctx, name):
        return self._sync_handle(args, exec_ctx, name, _ManagedSemaphore)

    def execute_nativeSemaphoreWait(self, args, exec_ctx):
        semaphore, error = self._semaphore(args, exec_ctx, "nativeSemaphoreWait")
        if error:
            return error
        with _SYNC_REGISTRY_LOCK:
            semaphore.waiters += 1
        try:
            semaphore.semaphore.acquire()
        finally:
            with _SYNC_REGISTRY_LOCK:
                semaphore.waiters -= 1
        return RTResult().success(Number.null)

    def execute_nativeSemaphoreTryWait(self, args, exec_ctx):
        semaphore, error = self._semaphore(args, exec_ctx, "nativeSemaphoreTryWait")
        if error:
            return error
        return RTResult().success(Number(
            1 if semaphore.semaphore.acquire(False) else 0, is_bool=True
        ))

    def execute_nativeSemaphorePost(self, args, exec_ctx):
        semaphore, error = self._semaphore(args, exec_ctx, "nativeSemaphorePost")
        if error:
            return error
        semaphore.semaphore.release()
        return RTResult().success(Number.null)

    def execute_nativeSemaphoreClose(self, args, exec_ctx):
        semaphore, error = self._semaphore(args, exec_ctx, "nativeSemaphoreClose")
        if error:
            return error
        with _SYNC_REGISTRY_LOCK:
            if semaphore.waiters:
                return self._failure(exec_ctx, "nativeSemaphoreClose() cannot close a waited semaphore")
            for handle, value in _SYNC_OBJECTS.items():
                if value is semaphore:
                    del _SYNC_OBJECTS[handle]
                    break
        return RTResult().success(Number.null)

    def execute_memoryTypeSize(self, args, exec_ctx):
        if len(args) != 1 or _memory_type(args[0]) not in _MEMORY_TYPES:
            return self._failure(exec_ctx, "memoryTypeSize(type) expects a supported memory type")
        result = self._cpp(_MEMORY_LIB.memoryTypeSize, [_memory_type(args[0])], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryTypeAlignment(self, args, exec_ctx):
        if len(args) != 1 or _memory_type(args[0]) not in _MEMORY_TYPES:
            return self._failure(exec_ctx, "memoryTypeAlignment(type) expects a supported memory type")
        result = self._cpp(
            _MEMORY_LIB.memoryTypeAlignment, [_memory_type(args[0])], exec_ctx
        )
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryReadEndian(self, args, exec_ctx):
        if (
            len(args) != 4
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or _memory_type(args[2]) not in _MEMORY_TYPES
            or not isinstance(args[3], String)
        ):
            return self._failure(
                exec_ctx,
                "memoryReadEndian(address, offset, type, order) expects "
                "an address, offset, supported type, and byte order",
            )
        result = self._cpp(
            _MEMORY_LIB.memoryReadEndian,
            [args[0].value, args[1].value, _memory_type(args[2]), args[3].value],
            exec_ctx,
        )
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryWriteEndian(self, args, exec_ctx):
        if (
            len(args) != 5
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or _memory_type(args[2]) not in _MEMORY_TYPES
            or not isinstance(args[3], String)
            or not isinstance(args[4], Number)
            or args[4].is_bool
        ):
            return self._failure(
                exec_ctx,
                "memoryWriteEndian(address, offset, type, order, value) expects "
                "an address, offset, supported type, byte order, and number",
            )
        result = self._cpp(
            _MEMORY_LIB.memoryWriteEndian,
            [
                args[0].value,
                args[1].value,
                _memory_type(args[2]),
                args[3].value,
                args[4].value,
            ],
            exec_ctx,
        )
        if isinstance(result, RTResult):
            return result
        return RTResult().success(Number.null)

    def execute_memoryBlockAllocate(self, args, exec_ctx):
        if (
            len(args) != 2
            or _memory_type(args[0]) not in _MEMORY_TYPES
            or not _native_nonnegative(args[1])
        ):
            return self._failure(
                exec_ctx,
                "memoryBlockAllocate(type, count) expects a supported type and non-negative count",
            )
        type_name = _memory_type(args[0])
        assert type_name is not None
        count = args[1].value
        size = _MEMORY_TYPES[type_name][0] * count
        result = self._cpp(_MEMORY_LIB.memoryBlockAllocate, [type_name, count], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryBlockView(self, args, exec_ctx):
        """Describe an existing native allocation as a typed array view.

        Views deliberately do not own the allocation.  The caller must keep
        the source allocation alive and free it only after the view is gone.
        """
        if (
            len(args) != 3
            or not _native_int(args[0]) or args[0].value < 0
            or _memory_type(args[1]) not in _MEMORY_TYPES
            or not _native_nonnegative(args[2])
        ):
            return self._failure(
                exec_ctx,
                "memoryBlockView(address, type, count) expects an address, "
                "supported type, and non-negative count",
            )
        address, type_name, count = args[0].value, _memory_type(args[1]), args[2].value
        error = self._check_memory_address(address, exec_ctx)
        if error:
            return error
        result = self._cpp(_MEMORY_LIB.memoryBlockView, [address, type_name, count], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryArrayAllocate(self, args, exec_ctx):
        return self.execute_memoryBlockAllocate(args, exec_ctx)

    def execute_memoryArrayView(self, args, exec_ctx):
        return self.execute_memoryBlockView(args, exec_ctx)

    def execute_memoryArrayGet(self, args, exec_ctx):
        return self.execute_memoryBlockGet(args, exec_ctx)

    def execute_memoryArraySet(self, args, exec_ctx):
        return self.execute_memoryBlockSet(args, exec_ctx)

    def execute_memoryArrayLength(self, args, exec_ctx):
        return self.execute_memoryBlockLength(args, exec_ctx)

    def execute_memoryViewGet(self, args, exec_ctx):
        return self.execute_memoryBlockGet(args, exec_ctx)

    def execute_memoryViewSet(self, args, exec_ctx):
        return self.execute_memoryBlockSet(args, exec_ctx)

    def execute_memoryViewLength(self, args, exec_ctx):
        return self.execute_memoryBlockLength(args, exec_ctx)

    # Preferred typed-memory vocabulary.  The older Block/Array/View names
    # remain available, but these names describe the operation directly.
    def execute_memoryTypedAllocate(self, args, exec_ctx):
        return self.execute_memoryBlockAllocate(args, exec_ctx)

    def execute_memoryTypedView(self, args, exec_ctx):
        return self.execute_memoryBlockView(args, exec_ctx)

    def execute_memoryTypedRead(self, args, exec_ctx):
        return self.execute_memoryBlockGet(args, exec_ctx)

    def execute_memoryTypedWrite(self, args, exec_ctx):
        return self.execute_memoryBlockSet(args, exec_ctx)

    def execute_memoryTypedLength(self, args, exec_ctx):
        return self.execute_memoryBlockLength(args, exec_ctx)

    def execute_memoryBlockGet(self, args, exec_ctx):
        if len(args) != 2 or not _native_nonnegative(args[0]) or not _native_nonnegative(args[1]):
            return self._failure(exec_ctx, "memoryBlockGet(address, index) expects non-negative integers")
        index = args[1].value
        result = self._cpp(_MEMORY_LIB.memoryBlockGet, [args[0].value, index], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryBlockSet(self, args, exec_ctx):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not isinstance(args[2], Number)
            or args[2].is_bool
        ):
            return self._failure(exec_ctx, "memoryBlockSet(address, index, value) expects an address, index, and number")
        index = args[1].value
        value = args[2].value
        result = self._cpp(_MEMORY_LIB.memoryBlockSet, [args[0].value, index, value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number.null)

    def execute_memoryBlockLength(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "memoryBlockLength(address) expects a non-negative integer address")
        result = self._cpp(_MEMORY_LIB.memoryBlockLength, [args[0].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructSize(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "memoryStructSize(layout) expects fields like \"int32 id, float32 x\"")
        result = self._cpp(_MEMORY_LIB.memoryStructSize, [args[0].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructFieldOffset(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], String) or not isinstance(args[1], String):
            return self._failure(
                exec_ctx,
                "memoryStructFieldOffset(layout, field) expects a layout and field name",
            )
        result = self._cpp(_MEMORY_LIB.memoryStructFieldOffset, [args[0].value, args[1].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructFieldSize(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], String) or not isinstance(args[1], String):
            return self._failure(
                exec_ctx,
                "memoryStructFieldSize(layout, field) expects a layout and field name",
            )
        result = self._cpp(_MEMORY_LIB.memoryStructFieldSize, [args[0].value, args[1].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructAlignment(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "memoryStructAlignment(layout) expects a layout string")
        result = self._cpp(_MEMORY_LIB.memoryStructAlignment, [args[0].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructFieldCount(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "memoryStructFieldCount(layout) expects a layout string")
        result = self._cpp(_MEMORY_LIB.memoryStructFieldCount, [args[0].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructFieldType(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], String)
            or not isinstance(args[1], String)
        ):
            return self._failure(
                exec_ctx,
                "memoryStructFieldType(layout, field) expects a layout and field name",
            )
        result = self._cpp(
            _MEMORY_LIB.memoryStructFieldType,
            [args[0].value, args[1].value],
            exec_ctx,
        )
        return result if isinstance(result, RTResult) else RTResult().success(String(result))

    # Explicit names for FFI callers.  The memoryStruct implementation uses
    # native alignment and native-endian primitive access, so these aliases
    # make that intent clear without creating a second layout format.
    def execute_nativeStructSize(self, args, exec_ctx):
        return self.execute_memoryStructSize(args, exec_ctx)

    def execute_nativeStructAllocate(self, args, exec_ctx):
        return self.execute_memoryStructAllocate(args, exec_ctx)

    def execute_nativeStructFieldOffset(self, args, exec_ctx):
        return self.execute_memoryStructFieldOffset(args, exec_ctx)

    def execute_nativeStructFieldSize(self, args, exec_ctx):
        return self.execute_memoryStructFieldSize(args, exec_ctx)

    def execute_nativeTypeAlignment(self, args, exec_ctx):
        return self.execute_memoryTypeAlignment(args, exec_ctx)

    def execute_nativeStructAlignment(self, args, exec_ctx):
        return self.execute_memoryStructAlignment(args, exec_ctx)

    def execute_nativeStructFieldCount(self, args, exec_ctx):
        return self.execute_memoryStructFieldCount(args, exec_ctx)

    def execute_nativeStructFieldType(self, args, exec_ctx):
        return self.execute_memoryStructFieldType(args, exec_ctx)

    def execute_nativeStructGet(self, args, exec_ctx):
        return self.execute_memoryStructGet(args, exec_ctx)

    def execute_nativeStructSet(self, args, exec_ctx):
        return self.execute_memoryStructSet(args, exec_ctx)

    def execute_memoryStructAllocate(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "memoryStructAllocate(layout) expects fields like \"int32 id, float32 x\"")
        result = self._cpp(_MEMORY_LIB.memoryStructAllocate, [args[0].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructGet(self, args, exec_ctx):
        if len(args) != 2 or not _native_nonnegative(args[0]) or not isinstance(args[1], String):
            return self._failure(exec_ctx, "memoryStructGet(address, field) expects an address and field name")
        result = self._cpp(_MEMORY_LIB.memoryStructGet, [args[0].value, args[1].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructSet(self, args, exec_ctx):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not isinstance(args[1], String)
            or not isinstance(args[2], Number)
            or args[2].is_bool
        ):
            return self._failure(exec_ctx, "memoryStructSet(address, field, value) expects an address, field, and number")
        result = self._cpp(_MEMORY_LIB.memoryStructSet, [args[0].value, args[1].value, args[2].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number.null)

    def execute_memoryAllocate(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "memoryAllocate(size) expects a non-negative integer size")
        result = self._cpp(_MEMORY_LIB.memoryAllocate, [args[0].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryAllocateZeroed(self, args, exec_ctx):
        if len(args) != 2 or not all(_native_nonnegative(arg) for arg in args):
            return self._failure(
                exec_ctx,
                "memoryAllocateZeroed(count, size) expects non-negative integer arguments",
            )
        result = self._cpp(
            _MEMORY_LIB.memoryAllocateZeroed, [args[0].value, args[1].value], exec_ctx
        )
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryReallocate(self, args, exec_ctx):
        if (
            len(args) != 2
            or not _native_nonnegative(args[1])
            or not _native_int(args[0])
            or args[0].value < 0
        ):
            return self._failure(
                exec_ctx,
                "memoryReallocate(address, size) expects an address and non-negative integer size",
            )
        error = self._check_memory_address(args[0].value, exec_ctx)
        if error:
            return error
        old_address = args[0].value
        result = self._cpp(
            _MEMORY_LIB.memoryReallocate, [old_address, args[1].value], exec_ctx
        )
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryFree(self, args, exec_ctx):
        if len(args) != 1 or not _native_int(args[0]) or args[0].value < 0:
            return self._failure(exec_ctx, "memoryFree(address) expects an integer address")
        address = args[0].value
        error = self._check_memory_address(address, exec_ctx)
        if error:
            return error
        result = self._cpp(_MEMORY_LIB.memoryFree, [address], exec_ctx)
        if isinstance(result, RTResult):
            return result
        return RTResult().success(Number.null)

    def _check_memory_address(self, address, exec_ctx):
        # Allocation ownership and lifetime are tracked by cpp.cpp.  Keeping
        # a second Python-side address set is incorrect because malloc may
        # legally reuse an address after free.
        return None

    def execute_memorySet(self, args, exec_ctx):
        if (
            len(args) != 3
            or not all(_native_nonnegative(arg) for arg in (args[0], args[2]))
            or not _native_int(args[1])
            or not 0 <= args[1].value <= 255
        ):
            return self._failure(
                exec_ctx,
                "memorySet(address, value, size) expects a non-negative address and size "
                "and a byte value from 0 to 255",
            )
        error = self._check_memory_address(args[0].value, exec_ctx)
        if error:
            return error
        result = self._cpp(
            _MEMORY_LIB.memorySet,
            [args[0].value, args[1].value, args[2].value],
            exec_ctx,
        )
        if isinstance(result, RTResult):
            return result
        return RTResult().success(Number.null)

    def execute_memoryCopy(self, args, exec_ctx):
        if (
            len(args) != 3
            or not all(_native_nonnegative(arg) for arg in args)
        ):
            return self._failure(
                exec_ctx,
                "memoryCopy(destination, source, size) expects non-negative integer arguments",
            )
        for address in (args[0].value, args[1].value):
            error = self._check_memory_address(address, exec_ctx)
            if error:
                return error
        result = self._cpp(
            _MEMORY_LIB.memoryCopy,
            [args[0].value, args[1].value, args[2].value],
            exec_ctx,
        )
        if isinstance(result, RTResult):
            return result
        return RTResult().success(Number.null)

    def _memory_read_builtin(self, args, exec_ctx, name, native_function):
        if (
            len(args) != 2
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
        ):
            return self._failure(
                exec_ctx,
                f"{name}(address, offset) expects non-negative integer arguments",
            )
        error = self._check_memory_address(args[0].value, exec_ctx)
        if error:
            return error
        result = self._cpp(
            native_function, [args[0].value, args[1].value], exec_ctx
        )
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def _memory_write_builtin(
        self, args, exec_ctx, name, native_function, minimum, maximum
    ):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not _native_int(args[2])
            or not minimum <= args[2].value <= maximum
        ):
            return self._failure(
                exec_ctx,
                f"{name}(address, offset, value) expects non-negative address "
                f"and offset and a value from {minimum} to {maximum}",
            )
        error = self._check_memory_address(args[0].value, exec_ctx)
        if error:
            return error
        result = self._cpp(
            native_function,
            [args[0].value, args[1].value, args[2].value],
            exec_ctx,
        )
        if isinstance(result, RTResult):
            return result
        return RTResult().success(Number.null)

    def execute_memoryReadInt8(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadInt8", _MEMORY_LIB.memoryReadInt8
        )

    def execute_memoryWriteInt8(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteInt8", _MEMORY_LIB.memoryWriteInt8, -(2**7), 2**7 - 1
        )

    def execute_memoryReadInt16(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadInt16", _MEMORY_LIB.memoryReadInt16
        )

    def execute_memoryWriteInt16(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteInt16", _MEMORY_LIB.memoryWriteInt16, -(2**15), 2**15 - 1
        )

    def execute_memoryReadInt32(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadInt32", _MEMORY_LIB.memoryReadInt32
        )

    def execute_memoryWriteInt32(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteInt32", _MEMORY_LIB.memoryWriteInt32, -(2**31), 2**31 - 1
        )

    def execute_memoryReadInt64(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadInt64", _MEMORY_LIB.memoryReadInt64
        )

    def execute_memoryWriteInt64(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteInt64", _MEMORY_LIB.memoryWriteInt64, -(2**63), 2**63 - 1
        )

    def execute_memoryReadUInt8(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadUInt8", _MEMORY_LIB.memoryReadUInt8
        )

    def execute_memoryWriteUInt8(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteUInt8", _MEMORY_LIB.memoryWriteUInt8, 0, 2**8 - 1
        )

    def execute_memoryReadUInt16(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadUInt16", _MEMORY_LIB.memoryReadUInt16
        )

    def execute_memoryWriteUInt16(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteUInt16", _MEMORY_LIB.memoryWriteUInt16, 0, 2**16 - 1
        )

    def execute_memoryReadUInt32(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadUInt32", _MEMORY_LIB.memoryReadUInt32
        )

    def execute_memoryWriteUInt32(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteUInt32", _MEMORY_LIB.memoryWriteUInt32, 0, 2**32 - 1
        )

    def execute_memoryReadUInt64(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadUInt64", _MEMORY_LIB.memoryReadUInt64
        )

    def execute_memoryWriteUInt64(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteUInt64", _MEMORY_LIB.memoryWriteUInt64, 0, 2**64 - 1
        )

    def _memory_read_float_builtin(self, args, exec_ctx, name, native_function):
        return self._memory_read_builtin(args, exec_ctx, name, native_function)

    def _memory_write_float_builtin(self, args, exec_ctx, name, native_function):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not isinstance(args[2], Number)
            or args[2].is_bool
        ):
            return self._failure(
                exec_ctx,
                f"{name}(address, offset, value) expects an address, offset, and number",
            )
        native_function(args[0].value, args[1].value, args[2].value)
        return RTResult().success(Number.null)

    def execute_memoryReadFloat32(self, args, exec_ctx):
        return self._memory_read_float_builtin(
            args, exec_ctx, "memoryReadFloat32", _MEMORY_LIB.memoryReadFloat32
        )

    def execute_memoryWriteFloat32(self, args, exec_ctx):
        return self._memory_write_float_builtin(
            args, exec_ctx, "memoryWriteFloat32", _MEMORY_LIB.memoryWriteFloat32
        )

    def execute_memoryReadFloat64(self, args, exec_ctx):
        return self._memory_read_float_builtin(
            args, exec_ctx, "memoryReadFloat64", _MEMORY_LIB.memoryReadFloat64
        )

    def execute_memoryWriteFloat64(self, args, exec_ctx):
        return self._memory_write_float_builtin(
            args, exec_ctx, "memoryWriteFloat64", _MEMORY_LIB.memoryWriteFloat64
        )

    def execute_memoryReadByte(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadByte", _MEMORY_LIB.memoryReadByte
        )

    def execute_memoryWriteByte(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteByte", _MEMORY_LIB.memoryWriteByte, 0, 255
        )

    def execute_sizeOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "sizeOf(typeName) expects one string")
        try:
            size = _MEMORY_LIB.sizeOf(args[0].value)
        except (TypeError, ValueError) as exc:
            return self._failure(exec_ctx, str(exc))
        return RTResult().success(Number(size))

    def execute_input(self, args, exec_ctx):
        if len(args) > 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "input() takes 0 or 1 arguments",
                    exec_ctx,
                )
            )
        prompt = str(args[0]) if args else ""
        text = input(prompt)
        return RTResult().success(String(text))

    def execute_inputln(self, args, exec_ctx):
        if len(args) > 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "inputln() takes 0 or 1 arguments",
                    exec_ctx,
                )
            )
        prompt = str(args[0]) if args else ""
        text = input(prompt)
        return RTResult().success(String(text + "\n"))

    def execute_rawPy(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'rawPy() expects exactly one string argument — rawPy("python code")',
                    exec_ctx,
                )
            )
        try:
            exec(args[0].value, {"__builtins__": __builtins__})
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Python error in rawPy(): {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    def execute_strOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "strOf() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        return RTResult().success(String(str(args[0])))

    def execute_intOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "intOf() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        try:
            return RTResult().success(Number(int(float(v.value))))
        except Exception:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cannot convert '{v}' to int",
                    exec_ctx,
                )
            )

    def execute_floatOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "floatOf() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        try:
            return RTResult().success(Number(float(v.value)))
        except Exception:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cannot convert '{v}' to float",
                    exec_ctx,
                )
            )

    def execute_sentinel(self, args, exec_ctx):
        """sentinel([name]) — create a unique, optionally named sentinel."""
        if len(args) > 1 or (args and not isinstance(args[0], String)):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'sentinel() expects zero or one string argument — sentinel("NAME")',
                    exec_ctx,
                )
            )
        name = args[0].value if args else None
        return RTResult().success(Sentinel(name).set_context(exec_ctx))

    def execute_object(self, args, exec_ctx):
        """object() — create a unique unnamed opaque object value."""
        if args:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "object() takes no arguments",
                    exec_ctx,
                )
            )
        return RTResult().success(ObjectValue().set_context(exec_ctx))

    def execute_rawPyx(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'rawPyx() expects exactly one string argument — rawPyx("cython code")',
                    exec_ctx,
                )
            )
        try:
            cython_inline = _get_cython_inline()
            cy_locals = {}
            cython_inline(args[0].value, locals=cy_locals, globals=cy_locals, quiet=True)
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cython error in rawPyx(): {type(e).__name__}: {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    def execute_returnType(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "returnType() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        return RTResult().success(String(_runtime.value_type_name(args[0])))

    def execute_returnLength(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "returnLength() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        if isinstance(v, String):
            return RTResult().success(Number(len(v.value)))
        if isinstance(v, (List, LynxTuple)):
            return RTResult().success(Number(len(v.elements)))
        return RTResult().failure(
            RTError(
                self.pos_start,
                self.pos_end,
                f"returnLength() does not support values of type '{type(v).__name__}'",
                exec_ctx,
            )
        )

    def execute_seqFromTo(self, args, exec_ctx):
        if len(args) != 3 or not all(isinstance(a, Number) for a in args):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "seqFromTo() expects exactly 3 numeric arguments — seqFromTo(start, stop, step)",
                    exec_ctx,
                )
            )
        start, stop, step = (int(a.value) for a in args)
        if step == 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "seqFromTo() step cannot be 0",
                    exec_ctx,
                )
            )
        elements = [Number(n).set_context(exec_ctx) for n in range(start, stop, step)]
        return RTResult().success(List(elements))

    def execute_range(self, args, exec_ctx):
        """range(stop), range(start, stop), or range(start, stop, step)."""
        if not args or len(args) > 3 or not all(isinstance(a, Number) for a in args):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "range() expects 1, 2, or 3 integer arguments: "
                    "range(stop), range(start, stop), or range(start, stop, step)",
                    exec_ctx,
                )
            )
        if len(args) == 1:
            start, stop, step = 0, int(args[0].value), 1
        elif len(args) == 2:
            start, stop, step = int(args[0].value), int(args[1].value), 1
        else:
            start, stop, step = (
                int(args[0].value),
                int(args[1].value),
                int(args[2].value),
            )
        if step == 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "range() step cannot be 0",
                    exec_ctx,
                )
            )
        elements = [Number(n).set_context(exec_ctx) for n in range(start, stop, step)]
        return RTResult().success(List(elements))

    def execute_cleanRawPyxCache(self, args, exec_ctx):
        import os
        import shutil

        cache_dir = os.path.expanduser("~/.cython/inline")
        try:
            if os.path.isdir(cache_dir):
                shutil.rmtree(cache_dir)
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"cleanRawPyxCache() failed: {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    # list built-ins

    def execute_listJsonArray(self, args, exec_ctx):
        import json as _json

        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJsonArray(list) expects a list",
                    exec_ctx,
                )
            )
        try:
            items = [_json_value(element) for element in args[0].elements]
            return RTResult().success(String(_json.dumps(items)))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listJsonArray() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_listJsonObject(self, args, exec_ctx):
        import json as _json

        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJsonObject(list) expects a flat key/value list",
                    exec_ctx,
                )
            )
        els = args[0].elements
        if len(els) % 2 != 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJsonObject() requires an even-length list (key, value, key, value, ...)",
                    exec_ctx,
                )
            )
        try:
            obj = {}
            for i in range(0, len(els), 2):
                k = _json_value(els[i])
                v = _json_value(els[i + 1])
                obj[str(k)] = v
            return RTResult().success(String(_json.dumps(obj)))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listJsonObject() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_splitStr(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], String)
            or not isinstance(args[1], String)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "splitStr(str, sep) expects two string arguments",
                    exec_ctx,
                )
            )
        parts = args[0].value.split(args[1].value)
        elements = [String(p).set_context(exec_ctx) for p in parts]
        return RTResult().success(List(elements))

    def execute_listFlatten(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listFlatten(list) expects a list",
                    exec_ctx,
                )
            )
        flat = []
        for el in args[0].elements:
            if isinstance(el, List):
                flat.extend(el.elements)
            else:
                flat.append(el)
        return RTResult().success(List(flat))

    def execute_listUnique(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listUnique(list) expects a list",
                    exec_ctx,
                )
            )
        seen_strs: list[str] = []
        unique_els = []
        for el in args[0].elements:
            s = str(el)
            if s not in seen_strs:
                seen_strs.append(s)
                unique_els.append(el)
        return RTResult().success(List(unique_els))

    def execute_listPush(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listPush(list, item) expects a list and a value",
                    exec_ctx,
                )
            )
        new_elements = list(args[0].elements) + [args[1]]
        return RTResult().success(List(new_elements))

    def execute_listPop(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listPop(list) expects a list",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listPop() called on an empty list",
                    exec_ctx,
                )
            )
        return RTResult().success(args[0].elements.pop())

    def execute_listGet(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listGet(list, idx) expects a list and an integer index",
                    exec_ctx,
                )
            )
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listGet() index {idx} out of range for list of length {len(lst.elements)}",
                    exec_ctx,
                )
            )
        return RTResult().success(lst.elements[idx])

    def execute_listSet(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listSet(list, idx, val) expects a list, an integer index, and a value",
                    exec_ctx,
                )
            )
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listSet() index {idx} out of range for list of length {len(lst.elements)}",
                    exec_ctx,
                )
            )
        new_elements = list(lst.elements)
        new_elements[idx] = args[2]
        return RTResult().success(List(new_elements))

    def execute_listSlice(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
            or not isinstance(args[2], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listSlice(list, start, stop) expects a list and two integer indices",
                    exec_ctx,
                )
            )
        start = int(args[1].value)
        stop = int(args[2].value)
        return RTResult().success(List(args[0].elements[start:stop]))

    def execute_listContains(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listContains(list, item) expects a list and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        found = any(str(e) == target for e in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_contains(self, args, exec_ctx):
        """contains(sequence, value) — membership for lists and tuples."""
        if len(args) != 2 or not isinstance(args[0], (List, LynxTuple)):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "contains(list_or_tuple, value) expects a list or tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        found = any(str(element) == target for element in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_listJoin(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], String)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJoin(list, sep) expects a list and a string separator",
                    exec_ctx,
                )
            )
        sep = args[1].value
        result = sep.join(str(e) for e in args[0].elements)
        return RTResult().success(String(result))

    def execute_listIndex(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listIndex(list, item) expects a list and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        for i, e in enumerate(args[0].elements):
            if str(e) == target:
                return RTResult().success(Number(i))
        return RTResult().success(Number(-1))

    def execute_listRemove(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listRemove(list, idx) expects a list and an integer index",
                    exec_ctx,
                )
            )
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listRemove() index {idx} out of range for list of length {len(lst.elements)}",
                    exec_ctx,
                )
            )
        new_elements = list(lst.elements)
        new_elements.pop(idx)
        return RTResult().success(List(new_elements))

    def execute_anyOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "anyOf(list) expects a list",
                    exec_ctx,
                )
            )
        result = any(e.is_true() for e in args[0].elements)
        return RTResult().success(Number(1 if result else 0, is_bool=True))

    def execute_allOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "allOf(list) expects a list",
                    exec_ctx,
                )
            )
        result = all(e.is_true() for e in args[0].elements)
        return RTResult().success(Number(1 if result else 0, is_bool=True))

    def execute_sumOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sumOf(list) expects a list",
                    exec_ctx,
                )
            )
        try:
            total = sum(e.value for e in args[0].elements if isinstance(e, Number))
            return RTResult().success(Number(total))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"sumOf() failed: {e}",
                    exec_ctx,
                )
            )

    def _list_sort_key(self, e):
        if isinstance(e, (Number, String)):
            return e.value
        return str(e)

    def execute_sortList(self, args, exec_ctx):
        if len(args) not in (1, 2) or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sortList(list) or sortList(list, reverse) expects a list",
                    exec_ctx,
                )
            )
        reverse = args[1].is_true() if len(args) == 2 else False
        try:
            sorted_els = sorted(
                args[0].elements, key=self._list_sort_key, reverse=reverse
            )
            return RTResult().success(List(sorted_els))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"sortList() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_reverseList(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "reverseList(list) expects a list",
                    exec_ctx,
                )
            )
        return RTResult().success(List(list(reversed(args[0].elements))))

    def execute_listMin(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMin(list) expects a list",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMin() called on an empty list",
                    exec_ctx,
                )
            )
        try:
            return RTResult().success(
                min(args[0].elements, key=self._list_sort_key)
            )
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listMin() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_listMax(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMax(list) expects a list",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMax() called on an empty list",
                    exec_ctx,
                )
            )
        try:
            return RTResult().success(
                max(args[0].elements, key=self._list_sort_key)
            )
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listMax() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_listFirst(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return self._failure(exec_ctx, "listFirst(list) expects a list")
        if not args[0].elements:
            return self._failure(exec_ctx, "listFirst() called on an empty list")
        return RTResult().success(args[0].elements[0])

    def execute_listLast(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return self._failure(exec_ctx, "listLast(list) expects a list")
        if not args[0].elements:
            return self._failure(exec_ctx, "listLast() called on an empty list")
        return RTResult().success(args[0].elements[-1])

    def execute_listHead(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return self._failure(
                exec_ctx, "listHead(list, count) expects a list and an integer count"
            )
        count = int(args[1].value)
        if count < 0:
            return self._failure(exec_ctx, "listHead() count cannot be negative")
        return RTResult().success(List(args[0].elements[:count]))

    def execute_listTail(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return self._failure(
                exec_ctx, "listTail(list, count) expects a list and an integer count"
            )
        count = int(args[1].value)
        if count < 0:
            return self._failure(exec_ctx, "listTail() count cannot be negative")
        return RTResult().success(List(args[0].elements[-count:] if count else []))

    def execute_listCount(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return self._failure(
                exec_ctx, "listCount(list, value) expects a list and a value"
            )
        target = str(args[1])
        return RTResult().success(
            Number(sum(1 for element in args[0].elements if str(element) == target))
        )

    def execute_listExtend(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], List)
        ):
            return self._failure(exec_ctx, "listExtend(list1, list2) expects two lists")
        return RTResult().success(List(args[0].elements + args[1].elements))

    def execute_listInsert(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return self._failure(
                exec_ctx,
                "listInsert(list, index, value) expects a list, integer index, and value",
            )
        elements = list(args[0].elements)
        elements.insert(int(args[1].value), args[2])
        return RTResult().success(List(elements))

    def execute_listClear(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return self._failure(exec_ctx, "listClear(list) expects a list")
        return RTResult().success(List([]))

    def execute_listRepeat(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[1], Number):
            return self._failure(
                exec_ctx, "listRepeat(value, count) expects a value and integer count"
            )
        count = int(args[1].value)
        if count < 0:
            return self._failure(exec_ctx, "listRepeat() count cannot be negative")
        return RTResult().success(List([args[0]] * count))

    def execute_listAvg(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return self._failure(exec_ctx, "listAvg(list) expects a list")
        numbers = [element.value for element in args[0].elements if isinstance(element, Number)]
        if not numbers:
            return RTResult().success(Number(0.0))
        return RTResult().success(Number(sum(numbers) / len(numbers)))

    def execute_listZip(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], List)
        ):
            return self._failure(exec_ctx, "listZip(list1, list2) expects two lists")
        import json as _json

        pairs = []
        for left, right in zip(args[0].elements, args[1].elements):
            pairs.append(
                String(
                    _json.dumps(
                        {"a": _json_value(left), "b": _json_value(right)}
                    )
                )
            )
        return RTResult().success(List(pairs))

    # tuple built-ins

    def execute_tupleCreate(self, args, exec_ctx):
        """tupleCreate(v1, v2, ...) — create a tuple from any number of arguments."""
        return RTResult().success(LynxTuple(args))

    def execute_tupleGet(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleGet(tuple, idx) expects a tuple and an integer index",
                    exec_ctx,
                )
            )
        t = args[0]
        idx = int(args[1].value)
        if idx < -len(t.elements) or idx >= len(t.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"tupleGet() index {idx} out of range for tuple of length {len(t.elements)}",
                    exec_ctx,
                )
            )
        return RTResult().success(t.elements[idx])

    def execute_tupleLen(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleLen(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(Number(len(args[0].elements)))

    def execute_tupleContains(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleContains(tuple, val) expects a tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        found = any(str(e) == target for e in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_tupleIndex(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleIndex(tuple, val) expects a tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        for i, e in enumerate(args[0].elements):
            if str(e) == target:
                return RTResult().success(Number(i))
        return RTResult().success(Number(-1))

    def execute_tupleSlice(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], Number)
            or not isinstance(args[2], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleSlice(tuple, start, stop) expects a tuple and two integer indices",
                    exec_ctx,
                )
            )
        start = int(args[1].value)
        stop = int(args[2].value)
        return RTResult().success(LynxTuple(args[0].elements[start:stop]))

    def execute_tupleToList(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleToList(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(List(list(args[0].elements)))

    def execute_listToTuple(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listToTuple(list) expects a list",
                    exec_ctx,
                )
            )
        return RTResult().success(LynxTuple(args[0].elements))

    def execute_tupleConcat(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], LynxTuple)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleConcat(t1, t2) expects two tuples",
                    exec_ctx,
                )
            )
        return RTResult().success(LynxTuple(args[0].elements + args[1].elements))

    def execute_tupleCount(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleCount(tuple, val) expects a tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        count = sum(1 for e in args[0].elements if str(e) == target)
        return RTResult().success(Number(count))

    def execute_tupleFirst(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleFirst(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleFirst() called on an empty tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(args[0].elements[0])

    def execute_tupleLast(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleLast(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleLast() called on an empty tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(args[0].elements[-1])

    def execute_tupleJsonArray(self, args, exec_ctx):
        import json as _json

        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleJsonArray(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        try:
            items = [_json_value(element) for element in args[0].elements]
            return RTResult().success(String(_json.dumps(items)))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"tupleJsonArray() failed: {e}",
                    exec_ctx,
                )
            )

    def _tuple_values(
        self, args: list[Any], exec_ctx: Any, name: str
    ) -> tuple[tuple[Any, ...] | None, Any]:
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return None, self._failure(exec_ctx, f"{name}(tuple) expects a tuple")
        return args[0].elements, None

    def execute_tupleReverse(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleReverse")
        if error:
            return error
        assert values is not None
        return RTResult().success(LynxTuple(reversed(values)))

    def execute_tupleSort(self, args, exec_ctx):
        if len(args) not in (1, 2) or not isinstance(args[0], LynxTuple):
            return self._failure(
                exec_ctx,
                "tupleSort(tuple) or tupleSort(tuple, reverse) expects a tuple",
            )
        reverse = args[1].is_true() if len(args) == 2 else False
        try:
            return RTResult().success(
                LynxTuple(sorted(args[0].elements, key=self._list_sort_key, reverse=reverse))
            )
        except Exception as exc:
            return self._failure(exec_ctx, f"tupleSort() failed: {exc}")

    def execute_tupleSortDesc(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "tupleSortDesc(tuple) expects a tuple")
        return self.execute_tupleSort([args[0], Number(1, is_bool=True)], exec_ctx)

    def execute_tupleMin(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleMin")
        if error:
            return error
        if not values:
            return self._failure(exec_ctx, "tupleMin() called on an empty tuple")
        try:
            return RTResult().success(min(values, key=self._list_sort_key))
        except Exception as exc:
            return self._failure(exec_ctx, f"tupleMin() failed: {exc}")

    def execute_tupleMax(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleMax")
        if error:
            return error
        if not values:
            return self._failure(exec_ctx, "tupleMax() called on an empty tuple")
        try:
            return RTResult().success(max(values, key=self._list_sort_key))
        except Exception as exc:
            return self._failure(exec_ctx, f"tupleMax() failed: {exc}")

    def execute_tupleSum(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleSum")
        if error:
            return error
        assert values is not None
        return RTResult().success(
            Number(sum(element.value for element in values if isinstance(element, Number)))
        )

    def execute_tupleAny(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleAny")
        if error:
            return error
        assert values is not None
        return RTResult().success(Number(int(any(value.is_true() for value in values)), is_bool=True))

    def execute_tupleAll(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleAll")
        if error:
            return error
        assert values is not None
        return RTResult().success(Number(int(all(value.is_true() for value in values)), is_bool=True))

    def execute_tupleUnique(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleUnique")
        if error:
            return error
        assert values is not None
        seen = set()
        unique = []
        for value in values:
            key = str(value)
            if key not in seen:
                seen.add(key)
                unique.append(value)
        return RTResult().success(LynxTuple(unique))

    def execute_tupleMean(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleMean")
        if error:
            return error
        assert values is not None
        numbers = [value.value for value in values if isinstance(value, Number)]
        return RTResult().success(Number(sum(numbers) / len(numbers) if numbers else 0.0))

    def execute_tupleFlatten(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleFlatten")
        if error:
            return error
        assert values is not None
        flattened = []
        for value in values:
            if isinstance(value, LynxTuple):
                flattened.extend(value.elements)
            else:
                flattened.append(value)
        return RTResult().success(LynxTuple(flattened))

    def execute_tupleZip(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], LynxTuple)
        ):
            return self._failure(exec_ctx, "tupleZip(tuple1, tuple2) expects two tuples")
        import json as _json

        pairs = []
        for left, right in zip(args[0].elements, args[1].elements):
            pairs.append(
                String(
                    _json.dumps(
                        {"a": _json_value(left), "b": _json_value(right)}
                    )
                )
            )
        return RTResult().success(List(pairs))

    def execute_tupleJoin(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], String)
        ):
            return self._failure(
                exec_ctx, "tupleJoin(tuple, separator) expects a tuple and string separator"
            )
        return RTResult().success(
            String(args[1].value.join(str(value) for value in args[0].elements))
        )

    # async I/O

    def _async_poll(
        self, args: list[Any], exec_ctx: Any, name: str
    ) -> "tuple[_AsyncPoll, RTResult | None]":
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return cast(_AsyncPoll, None), self._failure(
                exec_ctx, f"{name}(poll) expects a valid poll handle"
            )
        with _ASYNC_REGISTRY_LOCK:
            poll = _ASYNC_POLLS.get(args[0].value)
        if poll is None or poll.closed:
            return cast(_AsyncPoll, None), self._failure(
                exec_ctx, f"{name}() received an unknown or closed poll"
            )
        return poll, None

    def _async_timeout(self, value, exec_ctx, name, default=-1):
        if value is None:
            return default, None
        if not _native_int(value) or value.value < -1:
            return None, self._failure(
                exec_ctx, f"{name} timeout must be an integer >= -1 milliseconds"
            )
        return value.value, None

    def _async_fd(
        self, value: Any, exec_ctx: Any, name: str
    ) -> "tuple[int, RTResult | None]":
        if not _native_nonnegative(value):
            return cast(int, None), self._failure(
                exec_ctx, f"{name} resource expects a nonnegative integer"
            )
        resource = value.value
        if resource in _FILES:
            return _FILES[resource], None
        if resource in _SOCKETS:
            return _SOCKETS[resource].fileno(), None
        return resource, None

    def _async_event_mask(
        self, value: Any, exec_ctx: Any, name: str
    ) -> "tuple[tuple[str, int], RTResult | None]":
        if not isinstance(value, String):
            return cast(tuple[str, int], None), self._failure(
                exec_ctx, f"{name} events must be 'read', 'write', or 'readwrite'"
            )
        event_name = value.value.lower()
        masks = {
            "read": select.POLLIN,
            "write": select.POLLOUT,
            "readwrite": select.POLLIN | select.POLLOUT,
        }
        if event_name not in masks:
            return cast(tuple[str, int], None), self._failure(
                exec_ctx, f"{name} events must be 'read', 'write', or 'readwrite'"
            )
        return (event_name, masks[event_name]), None

    def execute_asyncPollCreate(self, args, exec_ctx):
        if args:
            return self._failure(exec_ctx, "asyncPollCreate() expects no arguments")
        poll = _AsyncPoll()
        handle = next(_ASYNC_IDS)
        with _ASYNC_REGISTRY_LOCK:
            _ASYNC_POLLS[handle] = poll
        return RTResult().success(Number(handle))

    def execute_asyncPollRegister(self, args, exec_ctx):
        if len(args) != 4 or not isinstance(args[3], String):
            return self._failure(
                exec_ctx,
                "asyncPollRegister(poll, resource, events, token) expects four arguments",
            )
        poll, error = self._async_poll(args[:1], exec_ctx, "asyncPollRegister")
        if error:
            return error
        fd, error = self._async_fd(args[1], exec_ctx, "asyncPollRegister")
        if error:
            return error
        event_info, error = self._async_event_mask(args[2], exec_ctx, "asyncPollRegister")
        if error:
            return error
        event_name, mask = event_info
        with poll.lock:
            if fd in poll.registrations:
                return self._failure(exec_ctx, "asyncPollRegister() resource is already registered")
            try:
                poll.poller.register(fd, mask)
            except OSError as exc:
                return self._failure(exec_ctx, f"asyncPollRegister() failed: {exc}")
            poll.registrations[fd] = {
                "token": args[3].value,
                "events": event_name,
            }
        return RTResult().success(Number.null)

    def execute_asyncPollModify(self, args, exec_ctx):
        if len(args) != 4 or not isinstance(args[3], String):
            return self._failure(
                exec_ctx,
                "asyncPollModify(poll, resource, events, token) expects four arguments",
            )
        poll, error = self._async_poll(args[:1], exec_ctx, "asyncPollModify")
        if error:
            return error
        fd, error = self._async_fd(args[1], exec_ctx, "asyncPollModify")
        if error:
            return error
        event_info, error = self._async_event_mask(args[2], exec_ctx, "asyncPollModify")
        if error:
            return error
        event_name, mask = event_info
        with poll.lock:
            if fd not in poll.registrations:
                return self._failure(exec_ctx, "asyncPollModify() resource is not registered")
            if "wakeup" in poll.registrations[fd]:
                return self._failure(exec_ctx, "asyncPollModify() cannot modify a wakeup")
            try:
                poll.poller.modify(fd, mask)
            except OSError as exc:
                return self._failure(exec_ctx, f"asyncPollModify() failed: {exc}")
            poll.registrations[fd].update({
                "token": args[3].value,
                "events": event_name,
            })
        return RTResult().success(Number.null)

    def execute_asyncPollRemove(self, args, exec_ctx):
        if len(args) != 2:
            return self._failure(
                exec_ctx, "asyncPollRemove(poll, resource) expects two arguments"
            )
        poll, error = self._async_poll(args[:1], exec_ctx, "asyncPollRemove")
        if error:
            return error
        fd, error = self._async_fd(args[1], exec_ctx, "asyncPollRemove")
        if error:
            return error
        with poll.lock:
            registration = poll.registrations.get(fd)
            if registration is None:
                return self._failure(exec_ctx, "asyncPollRemove() resource is not registered")
            if "wakeup" in registration:
                return self._failure(exec_ctx, "asyncPollRemove() cannot remove a wakeup")
            try:
                poll.poller.unregister(fd)
            except OSError as exc:
                return self._failure(exec_ctx, f"asyncPollRemove() failed: {exc}")
            del poll.registrations[fd]
        return RTResult().success(Number.null)

    def execute_asyncPollWait(self, args, exec_ctx):
        if len(args) not in {1, 2, 3}:
            return self._failure(
                exec_ctx, "asyncPollWait(poll, timeout_ms?, max_events?) expects one to three arguments"
            )
        poll, error = self._async_poll(args[:1], exec_ctx, "asyncPollWait")
        if error:
            return error
        timeout, error = self._async_timeout(
            args[1] if len(args) > 1 else None, exec_ctx, "asyncPollWait"
        )
        if error:
            return error
        max_events = 64
        if len(args) == 3:
            if not _native_nonnegative(args[2]) or args[2].value == 0:
                return self._failure(
                    exec_ctx, "asyncPollWait max_events must be a positive integer"
                )
            max_events = args[2].value

        async def _wait():
            try:
                events = await asyncio.to_thread(poll.wait, timeout, max_events)
            except Exception as exc:
                return RTResult().failure(RTError(
                    self.pos_start, self.pos_end,
                    f"asyncPollWait() failed: {exc}", exec_ctx,
                ))
            return RTResult().success(List([
                String(json.dumps(event, separators=(",", ":")))
                for event in events
            ]))

        return RTResult().success(CoroutineValue(_wait()))

    def execute_asyncPollDispatch(self, args, exec_ctx):
        if len(args) not in {2, 3, 4} or not hasattr(args[1], "execute"):
            return self._failure(
                exec_ctx,
                "asyncPollDispatch(poll, callback, timeout_ms?, max_events?) expects a poll and callback",
            )
        poll, error = self._async_poll(args[:1], exec_ctx, "asyncPollDispatch")
        if error:
            return error
        timeout, error = self._async_timeout(
            args[2] if len(args) > 2 else None, exec_ctx, "asyncPollDispatch"
        )
        if error:
            return error
        max_events = args[3].value if len(args) == 4 else 64
        if len(args) == 4 and (
            not _native_nonnegative(args[3]) or max_events == 0
        ):
            return self._failure(exec_ctx, "asyncPollDispatch max_events must be positive")
        callback = args[1]

        async def _dispatch():
            try:
                events = await asyncio.to_thread(poll.wait, timeout, max_events)
                for event in events:
                    result = callback.execute([
                        String(json.dumps(event, separators=(",", ":")))
                    ])
                    if result.error:
                        return result
                    if isinstance(result.value, CoroutineValue):
                        result = await result.value.coro
                        if result.error:
                            return result
                return RTResult().success(Number(len(events)))
            except Exception as exc:
                return RTResult().failure(RTError(
                    self.pos_start, self.pos_end,
                    f"asyncPollDispatch() failed: {exc}", exec_ctx,
                ))

        return RTResult().success(CoroutineValue(_dispatch()))

    def execute_asyncPollClose(self, args, exec_ctx):
        poll, error = self._async_poll(args, exec_ctx, "asyncPollClose")
        if error:
            return error
        with _ASYNC_REGISTRY_LOCK:
            wakeups = [
                (handle, wakeup) for handle, wakeup in _ASYNC_WAKEUPS.items()
                if wakeup.poll is poll
            ]
            for handle, _ in wakeups:
                _ASYNC_WAKEUPS.pop(handle, None)
        for _, wakeup in wakeups:
            try:
                wakeup.close()
            except OSError:
                pass
        try:
            poll.close()
        except RuntimeError as exc:
            return self._failure(exec_ctx, str(exc))
        with _ASYNC_REGISTRY_LOCK:
            for handle, value in list(_ASYNC_POLLS.items()):
                if value is poll:
                    del _ASYNC_POLLS[handle]
                    break
        return RTResult().success(Number.null)

    def execute_asyncTimerCreate(self, args, exec_ctx):
        if len(args) not in {3, 4} or not isinstance(args[2], String):
            return self._failure(
                exec_ctx,
                "asyncTimerCreate(poll, milliseconds, token, repeat_ms?) expects a poll, delay, and token",
            )
        poll, error = self._async_poll(args[:1], exec_ctx, "asyncTimerCreate")
        if error:
            return error
        if not _native_nonnegative(args[1]):
            return self._failure(exec_ctx, "asyncTimerCreate milliseconds must be nonnegative")
        repeat = 0
        if len(args) == 4:
            if not _native_nonnegative(args[3]):
                return self._failure(exec_ctx, "asyncTimerCreate repeat_ms must be nonnegative")
            repeat = args[3].value
        timer_id = next(_ASYNC_IDS)
        with poll.lock:
            poll.timers[timer_id] = {
                "deadline": time.monotonic() + args[1].value / 1000,
                "repeat": repeat / 1000,
                "token": args[2].value,
            }
        with _ASYNC_REGISTRY_LOCK:
            _ASYNC_TIMERS[timer_id] = poll
        return RTResult().success(Number(timer_id))

    def execute_asyncTimerCancel(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "asyncTimerCancel(timer) expects a valid timer handle")
        timer_id = args[0].value
        with _ASYNC_REGISTRY_LOCK:
            poll = _ASYNC_TIMERS.pop(timer_id, None)
        if poll is None:
            return self._failure(exec_ctx, "asyncTimerCancel() received an unknown or cancelled timer")
        with poll.lock:
            poll.timers.pop(timer_id, None)
        return RTResult().success(Number.null)

    def execute_asyncWakeupCreate(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[1], String):
            return self._failure(
                exec_ctx, "asyncWakeupCreate(poll, token) expects a poll and string token"
            )
        poll, error = self._async_poll(args[:1], exec_ctx, "asyncWakeupCreate")
        if error:
            return error
        try:
            with poll.lock:
                wakeup = _AsyncWakeup(poll, args[1].value)
        except OSError as exc:
            return self._failure(exec_ctx, f"asyncWakeupCreate() failed: {exc}")
        handle = next(_ASYNC_IDS)
        with _ASYNC_REGISTRY_LOCK:
            _ASYNC_WAKEUPS[handle] = wakeup
        return RTResult().success(Number(handle))

    def execute_asyncWakeupSignal(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "asyncWakeupSignal(wakeup) expects a valid handle")
        with _ASYNC_REGISTRY_LOCK:
            wakeup = _ASYNC_WAKEUPS.get(args[0].value)
        if wakeup is None:
            return self._failure(exec_ctx, "asyncWakeupSignal() received an unknown or closed wakeup")
        try:
            wakeup.signal()
        except OSError as exc:
            return self._failure(exec_ctx, f"asyncWakeupSignal() failed: {exc}")
        return RTResult().success(Number.null)

    def execute_asyncWakeupClose(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "asyncWakeupClose(wakeup) expects a valid handle")
        with _ASYNC_REGISTRY_LOCK:
            wakeup = _ASYNC_WAKEUPS.pop(args[0].value, None)
        if wakeup is None:
            return self._failure(exec_ctx, "asyncWakeupClose() received an unknown or closed wakeup")
        try:
            wakeup.close()
        except OSError as exc:
            return self._failure(exec_ctx, f"asyncWakeupClose() failed: {exc}")
        return RTResult().success(Number.null)

    # async built-ins

    def execute_asyncRun(self, args, exec_ctx):
        """asyncRun(coro) — run a coroutine."""
        import asyncio

        if len(args) != 1 or not isinstance(args[0], CoroutineValue):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "asyncRun(coro) expects a single coroutine argument "
                    "(the result of calling an 'async' function)",
                    exec_ctx,
                )
            )
        try:
            coro_res = asyncio.run(args[0].coro)
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"asyncRun() raised an exception: {type(e).__name__}: {e}",
                    exec_ctx,
                )
            )
        if isinstance(coro_res, RTResult):
            if coro_res.error:
                return RTResult().failure(coro_res.error)
            return RTResult().success(
                coro_res.value if coro_res.value is not None else Number.null
            )
        return RTResult().success(Number.null)

    def execute_asyncGather(self, args, exec_ctx):
        """asyncGather(coro1, coro2, ...) — return a coroutine."""
        for i, arg in enumerate(args):
            if not isinstance(arg, CoroutineValue):
                return RTResult().failure(
                    RTError(
                        self.pos_start,
                        self.pos_end,
                        f"asyncGather() argument {i + 1} is not a coroutine "
                        "(expected the result of calling an 'async' function)",
                        exec_ctx,
                    )
                )

        import asyncio

        coros = [arg.coro for arg in args]

        async def _gather():
            results = await asyncio.gather(*coros)
            elements = []
            for r in results:
                if isinstance(r, RTResult):
                    if r.error:
                        return r
                    elements.append(r.value if r.value is not None else Number.null)
                else:
                    elements.append(Number.null)
            return RTResult().success(List(elements))

        return RTResult().success(CoroutineValue(_gather()))

    def execute_sleep(self, args, exec_ctx):
        """sleep(seconds) — block the current execution for a number of seconds."""
        import time

        if len(args) != 1 or not isinstance(args[0], Number) or args[0].is_bool:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sleep(num) expects exactly one int or float argument",
                    exec_ctx,
                )
            )

        seconds = float(args[0].value)
        if seconds < 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sleep(num) cannot use a negative number of seconds",
                    exec_ctx,
                )
            )

        time.sleep(seconds)
        return RTResult().success(Number.null)

    def execute_asyncSleep(self, args, exec_ctx):
        """asyncSleep(seconds) — return a coroutine."""
        import asyncio

        if len(args) != 1 or not isinstance(args[0], Number):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "asyncSleep(seconds) expects a single numeric argument",
                    exec_ctx,
                )
            )
        seconds = args[0].value

        async def _sleep():
            await asyncio.sleep(seconds)
            return RTResult().success(Number.null)

        return RTResult().success(CoroutineValue(_sleep()))

    def execute_foreverDelay(self, args, exec_ctx):
        """foreverDelay(seconds) — configure the delay used by forever()."""
        if not _runtime._setup_in_progress:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "foreverDelay() may only be called inside global setup(){}",
                    exec_ctx,
                )
            )
        if len(args) != 1 or not isinstance(args[0], Number):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "foreverDelay(seconds) expects exactly one number",
                    exec_ctx,
                )
            )
        delay = float(args[0].value)
        if delay < 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "foreverDelay(seconds) cannot be negative",
                    exec_ctx,
                )
            )
        setattr(_runtime, "_forever_delay", delay)
        return RTResult().success(Number.null)

    def execute_suppressForeverWarning(self, args, exec_ctx):
        """Suppress the warning for forever() bodies without break."""
        if not _runtime._setup_in_progress:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "suppressForeverWarning() may only be called inside global setup(){}",
                    exec_ctx,
                )
            )
        if args:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "suppressForeverWarning() takes no arguments",
                    exec_ctx,
                )
            )
        setattr(_runtime, "_forever_warning_suppressed", True)
        return RTResult().success(Number.null)

    def execute_suppressDeprecationWarning(self, args, exec_ctx):
        """Suppress legacy syntax deprecation warnings for this run."""
        if not _runtime._setup_in_progress:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "suppressDeprecationWarning() may only be called inside global setup(){}",
                    exec_ctx,
                )
            )
        if args:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "suppressDeprecationWarning() takes no arguments",
                    exec_ctx,
                )
            )
        setattr(_runtime, "_deprecation_warning_suppressed", True)
        return RTResult().success(Number.null)

    def execute_overrideMain(self, args, exec_ctx):
        """overrideMain("funcName") — redirect the program."""
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "overrideMain() expects exactly one string argument — "
                    "the name of the global function to use as the program entry point.\n"
                    '  Example:  overrideMain("start");',
                    exec_ctx,
                )
            )
        setattr(_runtime, "_main_override", args[0].value)
        return RTResult().success(Number.null)

    def execute_assert(self, args, exec_ctx):
        """assert(condition[, message]) — fail when condition is false."""
        if len(args) not in (1, 2) or not isinstance(args[0], Number):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "assert(condition[, message]) expects a boolean or number "
                    "condition and an optional string message",
                    exec_ctx,
                )
            )
        if len(args) == 2 and not isinstance(args[1], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "assert(condition[, message]) expects message to be a string",
                    exec_ctx,
                )
            )
        if args[0].value == 0:
            message = args[1].value if len(args) == 2 else "Assertion failed"
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    message,
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)


BuiltinHandler = Callable[[BuiltInFunction, list[Any], Any], Any]

# Keep this list as the single source of truth for functions available to both
# programs and imported modules.  Adding a function here and registering its
# handler below is all that is needed to expose it everywhere.
def _execute_named_syscall(self, args, exec_ctx):
    """Dispatch a named Linux syscall through the C++ extension.

    The language-level API deliberately uses positional integer arguments,
    while the extension receives a list so one validated dispatcher can
    implement every syscall wrapper.
    """
    if len(args) > 6 or not all(_native_int(value) for value in args):
        return self._failure(
            exec_ctx,
            f"{self.name}(...) expects up to six integer arguments",
        )
    method = getattr(_MEMORY_LIB, self.name)
    result = self._cpp(method, [[value.value for value in args]], exec_ctx)
    if isinstance(result, RTResult):
        return result
    return RTResult().success(Number(result))


for _syscall_name in SYSCALL_BUILTIN_NAMES:
    setattr(BuiltInFunction, f"execute_{_syscall_name}", _execute_named_syscall)


BUILTIN_FUNCTION_NAMES = (
    "print",
    "println",
    "input",
    "inputln",
    "rawPy",
    "rawPyx",
    "strOf",
    "intOf",
    "floatOf",
    "sentinel",
    "object",
    "returnType",
    "returnLength",
    "seqFromTo",
    "range",
    "cleanRawPyxCache",
    "listJsonArray",
    "listJsonObject",
    "splitStr",
    "listFlatten",
    "listUnique",
    "listPush",
    "listPop",
    "listGet",
    "listSet",
    "listSlice",
    "listContains",
    "contains",
    "listJoin",
    "listIndex",
    "listRemove",
    "anyOf",
    "allOf",
    "sumOf",
    "sortList",
    "reverseList",
    "listMin",
    "listMax",
    "listFirst",
    "listLast",
    "listHead",
    "listTail",
    "listCount",
    "listExtend",
    "listInsert",
    "listClear",
    "listRepeat",
    "listAvg",
    "listZip",
    "asyncRun",
    "asyncGather",
    "asyncPollCreate",
    "asyncPollRegister",
    "asyncPollModify",
    "asyncPollRemove",
    "asyncPollWait",
    "asyncPollDispatch",
    "asyncPollClose",
    "asyncTimerCreate",
    "asyncTimerCancel",
    "asyncWakeupCreate",
    "asyncWakeupSignal",
    "asyncWakeupClose",
    "sleep",
    "asyncSleep",
    "foreverDelay",
    "suppressForeverWarning",
    "suppressDeprecationWarning",
    "tupleCreate",
    "tupleGet",
    "tupleLen",
    "tupleContains",
    "tupleIndex",
    "tupleSlice",
    "tupleToList",
    "listToTuple",
    "tupleConcat",
    "tupleCount",
    "tupleFirst",
    "tupleLast",
    "tupleJsonArray",
    "tupleReverse",
    "tupleSort",
    "tupleSortDesc",
    "tupleMin",
    "tupleMax",
    "tupleSum",
    "tupleAny",
    "tupleAll",
    "tupleUnique",
    "tupleMean",
    "tupleFlatten",
    "tupleZip",
    "tupleJoin",
    "assert",
    "overrideMain",
    "unshare",
    "getAddress",
    "modifyAddressValue",
    "getAddressValue",
    "functionAddress",
    "nativeFunctionAddress",
    "ffiLoadLibrary",
    "ffiLookup",
    "ffiCloseLibrary",
    "ffiCall",
    "ffiCallback",
    "ffiFreeCallback",
    "nativeModuleLoad",
    "nativeModuleName",
    "nativeModuleFunction",
    "nativeModuleConstant",
    "nativeModuleType",
    "nativeModuleError",
    "nativeModuleDependencies",
    "nativeModuleClose",
    "nativeThreadStart",
    "nativeThreadJoin",
    "nativeThreadIsAlive",
    "nativeThreadStatus",
    "nativeThreadDetach",
    "nativeMutexCreate",
    "nativeMutexLock",
    "nativeMutexTryLock",
    "nativeMutexUnlock",
    "nativeMutexClose",
    "nativeConditionCreate",
    "nativeConditionWait",
    "nativeConditionNotify",
    "nativeConditionNotifyAll",
    "nativeConditionClose",
    "nativeSemaphoreCreate",
    "nativeSemaphoreWait",
    "nativeSemaphoreTryWait",
    "nativeSemaphorePost",
    "nativeSemaphoreClose",
    "nativeHandleAllocate",
    "nativeHandleAddress",
    "nativeHandleFree",
    "nativeHandleIsAlive",
    "nativeCall",
    "processSpawn",
    "processWrite",
    "processCloseInput",
    "processRead",
    "processPoll",
    "processWait",
    "processSendSignal",
    "processClose",
    "filesystemOpen",
    "filesystemRead",
    "filesystemWrite",
    "filesystemClose",
    "filesystemStat",
    "filesystemList",
    "filesystemMkdir",
    "filesystemRemove",
    "filesystemRename",
    "filesystemLink",
    "filesystemReadLink",
    "filesystemChmod",
    "networkingOpen",
    "networkingBind",
    "networkingListen",
    "networkingAccept",
    "networkingConnect",
    "networkingSend",
    "networkingReceive",
    "networkingClose",
    "networkingShutdown",
    "networkingBlocking",
    "networkingOption",
    "networkingResolve",
    "networkingAddress",
    *SYSCALL_BUILTIN_NAMES,
    "atomicLoad",
    "atomicStore",
    "atomicAdd",
    "volatileRead",
    "volatileWrite",
    "memoryProtect",
    "memoryTypeSize",
    "memoryTypeAlignment",
    "memoryReadEndian",
    "memoryWriteEndian",
    "memoryBlockAllocate",
    "memoryBlockView",
    "memoryBlockGet",
    "memoryBlockSet",
    "memoryBlockLength",
    "memoryArrayAllocate",
    "memoryArrayView",
    "memoryArrayGet",
    "memoryArraySet",
    "memoryArrayLength",
    "memoryViewGet",
    "memoryViewSet",
    "memoryViewLength",
    "memoryStructSize",
    "memoryStructFieldOffset",
    "memoryStructFieldSize",
    "memoryStructAlignment",
    "memoryStructFieldCount",
    "memoryStructFieldType",
    "memoryStructAllocate",
    "memoryStructGet",
    "memoryStructSet",
    "nativeStructSize",
    "nativeStructAllocate",
    "nativeStructFieldOffset",
    "nativeStructFieldSize",
    "nativeTypeAlignment",
    "nativeStructAlignment",
    "nativeStructFieldCount",
    "nativeStructFieldType",
    "nativeStructGet",
    "nativeStructSet",
    "memoryAllocate",
    "memoryAllocateZeroed",
    "memoryReallocate",
    "memoryFree",
    "memorySet",
    "memoryCopy",
    "memoryReadInt32",
    "memoryWriteInt32",
    "memoryReadInt8",
    "memoryWriteInt8",
    "memoryReadInt16",
    "memoryWriteInt16",
    "memoryReadInt64",
    "memoryWriteInt64",
    "memoryReadUInt8",
    "memoryWriteUInt8",
    "memoryReadUInt16",
    "memoryWriteUInt16",
    "memoryReadUInt32",
    "memoryWriteUInt32",
    "memoryReadUInt64",
    "memoryWriteUInt64",
    "memoryReadFloat32",
    "memoryWriteFloat32",
    "memoryReadFloat64",
    "memoryWriteFloat64",
    "memoryReadByte",
    "memoryWriteByte",
    "sizeOf",
)


BUILTIN_FUNCTIONS: dict[str, BuiltInFunction] = {}


def register_builtin(name: str, handler: BuiltinHandler | None = None) -> BuiltInFunction:
    """Register and return a built-in function.

    ``handler`` is an optional callable receiving ``(builtin, args,
    exec_ctx)`` and returning an ``RTResult``. The common in-tree case is
    adding a name whose ``execute_<name>`` method is defined above.
    """
    if not name.isidentifier():
        raise ValueError(f"Invalid builtin name: {name!r}")
    if handler is not None:
        setattr(BuiltInFunction, f"execute_{name}", handler)
    function = BuiltInFunction(name)
    setattr(BuiltInFunction, name, function)
    BUILTIN_FUNCTIONS[name] = function
    # The global table is created after this module is imported, so the
    # startup registrations are installed by lynxer.py.  Extensions added
    # later should become available immediately as well.
    global_symbol_table = getattr(_runtime, "global_symbol_table", None)
    if global_symbol_table is not None:
        global_symbol_table.set(name, function)
    return function


def register_builtins(symbol_table: Any) -> None:
    """Install every registered builtin into a Lynxer symbol table."""
    for name, function in BUILTIN_FUNCTIONS.items():
        symbol_table.set(name, function)


def builtin(name: str) -> Callable[[BuiltinHandler], BuiltinHandler]:
    """Decorator for adding a new builtin implementation in this module."""
    def decorator(handler: BuiltinHandler) -> BuiltinHandler:
        register_builtin(name, handler)
        return handler

    return decorator


# Create the public instances from the complete implementation above.
for _name in BUILTIN_FUNCTION_NAMES:
    register_builtin(_name)

# If this module was imported first, lynxer.py had to defer registration
# while this module was still being initialized. Complete it now that all
# BuiltInFunction instances exist.
if getattr(_runtime, "_builtins_registration_deferred", False):
    _runtime._register_builtins(_runtime.global_symbol_table)
