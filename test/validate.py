#!/usr/bin/env python3
"""Broad regression validator for Lynxer.
BTW ALL generated Lynxer files are created below a temp dir and rm
auto. Existing ``test/*.lynx`` fixtures are checked but never edited
or deleted (obviously dude).
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import platform
import subprocess
import sys
import tempfile
import re
import threading
import socket
from pathlib import Path
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT / "lynxer" / "shell.py"
sys.path.insert(0, str(ROOT))

from lynxer.bytecode import compile_to_bytecode, run_bytecode  # noqa: E402
from lynxer.install import INSTALL_PATH, _is_elf, _matching_pids  # noqa: E402
from lynxer.lynxer import Error, RTError, run  # noqa: E402


class ValidationFailure(Exception):
    pass


def run_source(source: str, filename: str = "<validation>") -> tuple[str, Error | None]:
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        _, error = run(filename, source)
    return output.getvalue(), error


def require_output(source: str, expected: str, name: str) -> None:
    output, error = run_source(source, f"<{name}>")
    if error is not None:
        raise ValidationFailure(f"{name}: runtime error:\n{error.as_string()}")
    if output != expected:
        raise ValidationFailure(
            f"{name}: expected output {expected!r}, received {output!r}"
        )


def require_error(source: str, fragment: str, name: str) -> None:
    _, error = run_source(source, f"<{name}>")
    if error is None:
        raise ValidationFailure(f"{name}: expected an error containing {fragment!r}")
    rendered = error.as_string()
    if fragment not in rendered:
        raise ValidationFailure(
            f"{name}: error did not contain {fragment!r}:\n{rendered}"
        )


def test_scalars_and_operators() -> None:
    require_output(
        """global setup(){}
global main(){
    int a = 7;
    float b = 2.5;
    bool ok = a > 3 and b < 3;
    println(a + 5);
    println(strOf(b));
    println(ok);
}""",
        "12\n2.5\ntrue\n",
        "scalars and operators",
    )


def test_lists_and_tuples() -> None:
    require_output(
        """global setup(){}
global main(){
    list values = [int 1, str "two", list [bool true]];
    tuple pair = (int 3, str "four");
    println(strOf(values));
    println(strOf(pair));
    println(returnLength(values));
    println(tupleLen(pair));
}""",
        "[1, two, [true]]\n(3, four)\n3\n2\n",
        "lists and tuples",
    )
    require_error(
        """global setup(){}
global main(){ list values = [int "wrong"]; }""",
        "declared as 'int'",
        "list type validation",
    )


def test_shared_aliases() -> None:
    require_output(
        """global setup(){}
global main(){
    int x = 42;
    shared int y = x;
    println(y);
    y = 100;
    println(x);
    unshare(y);
    y = 200;
    println(x);
    println(y);
}""",
        "42\n100\n100\n200\n",
        "shared aliases",
    )
    require_error(
        """global setup(){}
global main(){ int x = 1; shared int y = x + 1; }""",
        "must be initialized from another variable",
        "shared initializer validation",
    )
    require_output(
        """global setup(){}
global change(int x){
    shared x;
    x = 100;
}
global main(){
    int y = 42;
    change(y);
    println(y);
}""",
        "100\n",
        "shared function parameter",
    )


def test_control_flow_and_functions() -> None:
    require_output(
        """global setup(){}
global add(int a, int b){ return a + b; }
global main(){
    int total = 0;
    for(int i = 0; i < 4; i += 1){ total += i; }
    println(total);
    println(global.add(2, 3));
}""",
        "6\n5\n",
        "control flow and functions",
    )


def test_runtime_errors() -> None:
    require_error(
        """global setup(){}
global main(){ int value = "not an int"; }""",
        "Type mismatch",
        "runtime type error",
    )
    require_error(
        """global setup(){}
global main(){ int value = 1 / 0; }""",
        "Division by zero",
        "division by zero",
    )
    require_error(
        """global setup(){}
global main(){ println(missing); }""",
        "is not defined",
        "undefined variable",
    )


def test_low_level_memory() -> None:
    source_path = ROOT / "test" / "test20.lynx"
    source = source_path.read_text(encoding="utf-8")
    expected = (
        "-8\n"
        "-1600\n"
        "-320000\n"
        "-6400000000\n"
        "250\n"
        "65000\n"
        "4000000000\n"
        "16000000000\n"
        "1.5\n"
        "2.5\n"
        "4\n"
    )
    output, error = run_source(source, str(source_path))
    if error is not None:
        raise ValidationFailure(
            f"low-level memory fixture failed:\n{error.as_string()}"
        )
    if output != expected:
        raise ValidationFailure(
            f"low-level memory fixture: expected {expected!r}, received {output!r}"
        )

    require_error(
        """global setup(){}
global main(){ memoryWriteUInt8(1, 0, 256); }""",
        "0 to 255",
        "memory byte range validation",
    )
    require_error(
        """global setup(){}
global main(){ memoryReadInt32(-1, 0); }""",
        "non-negative integer arguments",
        "memory address validation",
    )
    require_error(
        """global setup(){}
global main(){ println(sizeOf("unit32")); }""",
        "unknown C type",
        "uint32 spelling validation",
    )
    require_output(
        """global setup(){}
global main(){
    int values = memoryBlockAllocate("int32", 2);
    memoryBlockSet(values, 0, 7);
    memoryBlockView(values, "int32", 2);
    memoryViewSet(values, 1, 9);
    println(memoryArrayGet(values, 0));
    println(memoryViewGet(values, 1));
    println(memoryArrayLength(values));
    memoryFree(values);
}""",
        "7\n9\n2\n",
        "native typed memory blocks and views",
    )
    require_output(
        """global setup(){}
global main(){
    int a = memoryBlockAllocate("int8", 1);
    int b = memoryBlockAllocate("uint8", 1);
    int c = memoryBlockAllocate("int16", 1);
    int d = memoryBlockAllocate("uint16", 1);
    int e = memoryBlockAllocate("int32", 1);
    int f = memoryBlockAllocate("uint32", 1);
    int g = memoryBlockAllocate("int64", 1);
    int h = memoryBlockAllocate("uint64", 1);
    int i = memoryBlockAllocate("float32", 1);
    int j = memoryBlockAllocate("float64", 1);
    memoryBlockSet(a, 0, -8); memoryBlockSet(b, 0, 250);
    memoryBlockSet(c, 0, -1600); memoryBlockSet(d, 0, 65000);
    memoryBlockSet(e, 0, -320000); memoryBlockSet(f, 0, 4000000000);
    memoryBlockSet(g, 0, -6400000000); memoryBlockSet(h, 0, 16000000000);
    memoryBlockSet(i, 0, 1.5); memoryBlockSet(j, 0, 2.5);
    println(memoryBlockGet(a, 0)); println(memoryBlockGet(b, 0));
    println(memoryBlockGet(c, 0)); println(memoryBlockGet(d, 0));
    println(memoryBlockGet(e, 0)); println(memoryBlockGet(f, 0));
    println(memoryBlockGet(g, 0)); println(memoryBlockGet(h, 0));
    println(memoryBlockGet(i, 0)); println(memoryBlockGet(j, 0));
    memoryFree(a); memoryFree(b); memoryFree(c); memoryFree(d); memoryFree(e);
    memoryFree(f); memoryFree(g); memoryFree(h); memoryFree(i); memoryFree(j);
}""",
        "-8\n250\n-1600\n65000\n-320000\n4000000000\n"
        "-6400000000\n16000000000\n1.5\n2.5\n",
        "all native typed memory scalar types",
    )
    require_output(
        """global setup(){}
global main(){
    int raw = memoryAllocate(16);
    memoryBlockView(raw, "int32", 4);
    memoryBlockSet(raw, 0, 11);
    memoryArraySet(raw, 1, 22);
    println(memoryViewGet(raw, 0));
    println(memoryArrayGet(raw, 1));
    println(memoryBlockLength(raw));
    memoryFree(raw);
}""",
        "11\n22\n4\n",
        "typed views and array aliases",
    )
    require_output(
        """global setup(){}
global main(){
    int player = memoryStructAllocate("int32 id, float32 x");
    memoryStructSet(player, "id", 42);
    memoryStructSet(player, "x", 2.5);
    println(memoryStructGet(player, "id"));
    println(memoryStructGet(player, "x"));
    println(memoryStructSize("int32 id, float32 x"));
    println(memoryStructFieldOffset("int32 id, float32 x", "x"));
    memoryFree(player);
}""",
        "42\n2.5\n8\n4\n",
        "native struct layouts",
    )
    require_output(
        """global setup(){}
global main(){
    int player = nativeStructAllocate("int32 id, float64 score, uint8 alive");
    nativeStructSet(player, "id", 99);
    nativeStructSet(player, "score", 12.5);
    nativeStructSet(player, "alive", 1);
    println(nativeStructGet(player, "id"));
    println(nativeStructGet(player, "score"));
    println(nativeStructGet(player, "alive"));
    println(nativeStructFieldOffset("int32 id, float64 score, uint8 alive", "score"));
    println(nativeStructFieldSize("int32 id, float64 score, uint8 alive", "alive"));
    memoryFree(player);
}""",
        "99\n12.5\n1\n8\n1\n",
        "native struct aliases and padding",
    )
    require_output(
        """global setup(){}
global main(){
    println(memoryTypeAlignment("float64"));
    println(memoryStructAlignment("int32 id, float64 score, uint8 alive"));
    println(memoryStructFieldCount("int32 id, float64 score, uint8 alive"));
    println(memoryStructFieldType("int32 id, float64 score, uint8 alive", "score"));
    println(nativeStructFieldType("int32 id, float64 score, uint8 alive", "alive"));
}""",
        "8\n8\n3\nfloat64\nuint8\n",
        "native alignment and layout introspection",
    )
    require_output(
        """global setup(){}
global main(){
    str layout = "struct{int32 x, uint8 y} nested, union{int32 i, float64 d} choice, int32[3] values, functionPointer callback";
    println(memoryStructSize(layout));
    println(memoryStructAlignment(layout));
    println(memoryStructFieldOffset(layout, "nested"));
    println(memoryStructFieldSize(layout, "nested"));
    println(memoryStructFieldOffset(layout, "choice"));
    println(memoryStructFieldSize(layout, "choice"));
    println(memoryStructFieldOffset(layout, "values"));
    println(memoryStructFieldSize(layout, "values"));
    println(memoryStructFieldOffset(layout, "callback"));
    println(memoryStructFieldSize(layout, "callback"));
    int value = memoryStructAllocate(layout);
    memoryWriteInt32(value, 16, 41);
    memoryWriteInt32(value, 20, 42);
    memoryWriteInt32(value, 24, 43);
    memoryStructSet(value, "callback", 123);
    println(memoryReadInt32(value, 16));
    println(memoryReadInt32(value, 24));
    println(memoryStructGet(value, "callback"));
    memoryFree(value);
}""",
        "40\n8\n0\n8\n8\n8\n16\n12\n32\n8\n41\n43\n123\n",
        "C ABI nested structs unions arrays and function pointers",
    )
    require_output(
        """global setup(){}
global main(){
    int address = memoryAllocate(8);
    memoryWriteEndian(address, 0, "uint32", "big", 305419896);
    println(memoryReadEndian(address, 0, "uint32", "big"));
    println(memoryReadEndian(address, 0, "uint32", "little"));
    memoryWriteEndian(address, 0, "float32", "little", 1.5);
    println(memoryReadEndian(address, 0, "float32", "little"));
    memoryFree(address);
}""",
        "305419896\n2018915346\n1.5\n",
        "explicit native byte order",
    )
    require_error(
        """global setup(){}
global main(){
    int values = memoryBlockAllocate("int32", 1);
    memoryBlockGet(values, 1);
}""",
        "out of bounds",
        "typed block bounds validation",
    )
    require_error(
        """global setup(){}
global main(){
    int player = memoryStructAllocate("int32 id");
    memoryStructGet(player, "missing");
}""",
        "struct field is not present",
        "struct field validation",
    )
    require_error(
        """global setup(){}
global main(){
    int values = memoryBlockAllocate("uint8", 1);
    memoryBlockSet(values, 0, 256);
}""",
        "outside the range",
        "typed block range validation",
    )
    require_error(
        """global setup(){}
global main(){
    int values = memoryAllocate(4);
    memoryBlockView(values, "int32", 2);
}""",
        "out of bounds",
        "typed view allocation bounds validation",
    )
    require_error(
        """global setup(){}
global main(){
    int values = memoryBlockAllocate("int32", 1);
    memoryFree(values);
    memoryBlockLength(values);
}""",
        "typed memory block",
        "typed block lifetime validation",
    )
    require_error(
        """global setup(){}
global main(){
    int values = memoryBlockAllocate("int32", 1);
    memoryBlockGet(values, 1);
}""",
        "out of bounds",
        "typed block index validation",
    )
    require_error(
        """global setup(){}
global main(){
    int player = memoryStructAllocate("int32 id");
    memoryStructSet(player, "missing", 1);
}""",
        "struct field is not present",
        "struct field write validation",
    )


def test_imports(temp_root: Path) -> None:
    module = temp_root / "validation_module.lynx"
    module.write_text(
        """global setup(){ int exported = 9; }
global helper(){ return 4; }
""",
        encoding="utf-8",
    )
    source = """global setup(){ importAs("validation_module.lynx", "mod"); }
global main(){{ println(global.mod.exported); println(global.mod.helper()); }}
""".replace("global main(){{", "global main(){").replace("}}", "}")
    old_cwd = Path.cwd()
    os.chdir(temp_root)
    try:
        output, error = run_source(source, str(temp_root / "import_main.lynx"))
    finally:
        os.chdir(old_cwd)
    if error is not None:
        raise ValidationFailure(f"imports: runtime error:\n{error.as_string()}")
    if output != "9\n4\n":
        raise ValidationFailure(f"imports: received {output!r}")

    nested = temp_root / "module_tree"
    nested.mkdir()
    (nested / "leaf.lynx").write_text(
        """global setup(){}
global value(){ return 12; }
""",
        encoding="utf-8",
    )
    (nested / "middle.lynx").write_text(
        """global setup(){ import("leaf.lynx"); }
global read(){ return global.leaf.value(); }
""",
        encoding="utf-8",
    )
    nested_source = """global setup(){ import("module_tree/middle.lynx"); }
global main(){ println(global.middle.read()); }
"""
    output, error = run_source(
        nested_source, str(temp_root / "nested_import_main.lynx")
    )
    if error is not None:
        raise ValidationFailure(f"nested imports: runtime error:\n{error.as_string()}")
    if output != "12\n":
        raise ValidationFailure(f"nested imports: received {output!r}")

    (temp_root / "one").mkdir()
    (temp_root / "two").mkdir()
    for directory in ("one", "two"):
        (temp_root / directory / "same_name.lynx").write_text(
            "global setup(){}\nglobal main(){}\n",
            encoding="utf-8",
        )
    collision_source = """global setup(){
    import("one/same_name.lynx");
    import("two/same_name.lynx");
}
global main(){}
"""
    _, error = run_source(
        collision_source, str(temp_root / "collision_main.lynx")
    )
    if error is None or "Module name collision" not in error.as_string():
        raise ValidationFailure("module name collisions were not rejected")


def test_game_stdlib(temp_root: Path) -> None:
    source = """global setup(){ import("game"); }
global main(){}
"""
    output, error = run_source(source, str(temp_root / "game_stdlib.lynx"))
    if error is not None:
        raise ValidationFailure(f"game stdlib: runtime error:\n{error.as_string()}")
    if output:
        raise ValidationFailure(f"game stdlib: received unexpected output {output!r}")


def test_native_modules(temp_root: Path) -> None:
    module_path = temp_root / "validation_native.so"
    c_source = r"""
#include <stdint.h>
typedef int (*register_function)(const char *, const char *, const char *);
typedef int (*register_constant)(const char *, int64_t);
typedef int (*register_type)(const char *, const char *);
int add_values(int64_t a, int64_t b) { return (int)(a + b); }
int lynxer_module_init_v1(register_function function,
                          register_constant constant,
                          register_type type) {
    if (!function("add", "add_values", "cdecl:int32(int64,int64)")) return 1;
    if (!constant("magic", 42)) return 2;
    if (!type("pair", "int64 left, int64 right")) return 3;
    return 0;
}
"""
    source_path = temp_root / "validation_native.c"
    source_path.write_text(c_source, encoding="utf-8")
    result = subprocess.run(
        ["cc", "-shared", "-fPIC", "-O2", str(source_path), "-o", str(module_path)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise ValidationFailure(f"native module compilation failed: {result.stderr}")

    module_name = json.dumps(module_path.name)
    module_path_text = json.dumps(module_path.name)
    old_cwd = Path.cwd()
    os.chdir(temp_root)
    try:
        require_output(
            f"""global setup(){{ importAs({module_name}, "nmod"); }}
global main(){{
    println(global.nmod.magic);
    println(global.nmod.add(2, 3));
    println(global.nmod.pair);
    int module = nativeModuleLoad({module_path_text});
    println(nativeModuleName(module));
     println(nativeModuleError(module) == "");
     println(returnLength(nativeModuleDependencies(module)) >= 0);
    println(nativeModuleConstant(module, "magic"));
    println(nativeModuleType(module, "pair"));
    functionAddress add = nativeModuleFunction(module, "add");
    println(ffiCall(add, "cdecl:int32(int64,int64)", [int 7, int 8]));
    nativeModuleClose(module);
}}""",
         "42\n5\nint64 left, int64 right\nvalidation_native\ntrue\ntrue\n42\nint64 left, int64 right\n15\n",
            "native module registration and lifecycle",
        )
        require_error(
            f"""global setup(){{}}
global main(){{
    int module = nativeModuleLoad({module_path_text});
    functionAddress add = nativeModuleFunction(module, "add");
    nativeModuleClose(module);
    ffiCall(add, "cdecl:int32(int64,int64)", [int 1, int 2]);
}}""",
            "closed native module",
            "native module unload safety",
        )
    finally:
        os.chdir(old_cwd)


def test_bytecode(temp_root: Path) -> None:
    source_path = temp_root / "bytecode_case.lynx"
    source = """global setup(){}
global main(){
    int x = 42;
    shared int y = x;
    unshare(y);
    y = 100;
    println(x);
    println(y);
}"""
    source_path.write_text(source, encoding="utf-8")
    bytecode_path, error = compile_to_bytecode(str(source_path), source)
    if error is not None or bytecode_path is None:
        raise ValidationFailure(
            f"bytecode compilation failed: {error.as_string() if error else 'unknown error'}"
        )
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        _, error = run_bytecode(bytecode_path)
    if error is not None:
        raise ValidationFailure(f"bytecode execution failed:\n{error.as_string()}")
    if output.getvalue() != "42\n100\n":
        raise ValidationFailure(f"bytecode: received {output.getvalue()!r}")


def test_ffi() -> None:
    require_output(
        """global setup(){}
global add(int a, int b){ return a + b; }
global main(){
    int libc = ffiLoadLibrary("libc.so.6");
    functionAddress strlen = ffiLookup(libc, "strlen");
    println(ffiCall(strlen, "cdecl:uintptr(cstring)", [str "hello"]));
    functionAddress callback = ffiCallback("cdecl:int32(int32,int32)", global.add);
    println(ffiCall(callback, "cdecl:int32(int32,int32)", [int 2, int 3]));
    ffiFreeCallback(callback);
    ffiCloseLibrary(libc);
}""",
        "5\n5\n",
        "C FFI and native callbacks",
    )


def test_native_threads() -> None:
    require_output(
        """global setup(){}
global worker(int value){ println(value); }
global main(){
    int thread = nativeThreadStart(global.worker, [int 42]);
    nativeThreadJoin(thread);
}""",
        "42\n",
        "native thread start and join",
    )
    output, error = run_source(
        """global setup(){}
global worker(){ assert(false, "thread failure"); }
global main(){
    int thread = nativeThreadStart(global.worker, []);
    println(nativeThreadJoin(thread));
}""",
        "<native thread failure status>",
    )
    if error is not None or "thread failure" not in output:
        raise ValidationFailure(
            "native thread failure status: expected propagated callback error, "
            f"received {output!r}"
        )
    require_output(
        """global setup(){}
global signaler(int mutex, int condition){
    nativeMutexLock(mutex);
    nativeConditionNotify(condition, mutex);
    nativeMutexUnlock(mutex);
}
global main(){
    int mutex = nativeMutexCreate();
    int condition = nativeConditionCreate();
    int semaphore = nativeSemaphoreCreate(0);
    nativeMutexLock(mutex);
    println(nativeMutexTryLock(mutex) == false);
    int thread = nativeThreadStart(global.signaler, [int mutex, int condition]);
    nativeConditionWait(condition, mutex);
    nativeMutexUnlock(mutex);
    nativeThreadJoin(thread);
    nativeSemaphorePost(semaphore);
    println(nativeSemaphoreTryWait(semaphore));
    nativeSemaphoreClose(semaphore);
    nativeConditionClose(condition);
    nativeMutexClose(mutex);
}""",
        "true\ntrue\n",
        "native synchronization primitives",
    )


def test_async_io(temp_root: Path) -> None:
    source_file = temp_root / "async_io.txt"
    source_file.write_text("ready", encoding="utf-8")
    source_path = json.dumps(str(source_file))
    source = f"""global setup(){{}}
global onEvent(str event){{ println(returnLength(event) > 0); }}
global main(){{
    int poll = asyncPollCreate();
    int file = filesystemOpen({source_path}, "r");
    async run(){{
        asyncPollRegister(poll, file, "read", "file");
        any fileEvents = await asyncPollWait(poll, 1000, 4);
        println(returnLength(fileEvents) > 0);
        asyncPollModify(poll, file, "readwrite", "modified");
        asyncPollRemove(poll, file);
        int timer = asyncTimerCreate(poll, 1, "timer");
        any timerEvents = await asyncPollWait(poll, 1000);
        println(returnLength(timerEvents) > 0);
        asyncTimerCancel(timer);
        int wakeup = asyncWakeupCreate(poll, "wake");
        asyncWakeupSignal(wakeup);
        any wakeEvents = await asyncPollWait(poll, 1000);
        println(returnLength(wakeEvents) > 0);
        asyncWakeupSignal(wakeup);
        await asyncPollDispatch(poll, global.onEvent, 1000, 1);
        asyncWakeupClose(wakeup);
    }}
    async.run();
    filesystemClose(file);
    asyncPollClose(poll);
}}"""
    output, error = run_source(source, str(temp_root / "async_io.lynx"))
    if error is not None:
        raise ValidationFailure(f"async I/O: runtime error:\n{error.as_string()}")
    if output != "true\ntrue\ntrue\ntrue\n":
        raise ValidationFailure(f"async I/O: received {output!r}")


def test_linux_syscall() -> None:
    require_output(
        """global setup(){}
global main(){
    println(syscallGetProcessId() > 0);
    println(syscallGetParentProcessId() > 0);
    println(syscallYieldProcessor());
    int randomBytes = memoryAllocate(16);
    println(syscallGetRandomBytes(randomBytes, 16, 0) == 16);
    println(memoryReadUInt8(randomBytes, 0) >= 0);
    memoryFree(randomBytes);
    int clock = memoryAllocate(16);
    println(syscallGetClockTime(0, clock) == 0);
    memoryFree(clock);
    int systemInfo = memoryAllocate(256);
    println(syscallGetSystemInformation(systemInfo) == 0);
    memoryFree(systemInfo);
}""",
        "true\ntrue\n0\ntrue\ntrue\ntrue\ntrue\n",
        "named Linux syscall ABI and pointer arguments",
    )
    require_error(
        """global setup(){}
global main(){ syscallClose(-1); }""",
        "Bad file descriptor",
        "named syscall errno propagation",
    )


def test_process_api() -> None:
    executable = json.dumps(sys.executable)
    child = json.dumps(
        "import os,sys; data=sys.stdin.read(); "
        "sys.stdout.write(os.environ['LYNXER_PROCESS_TEST'] + ':' + data)"
    )
    require_output(
        f"""global setup(){{}}
global main(){{
    int process = processSpawn({executable}, [str "-c", str {child}], [str "LYNXER_PROCESS_TEST=ok"]);
    println(processPoll(process) == -1);
    println(processWrite(process, "hello") == 5);
    processCloseInput(process);
    println(processWait(process, 2) == 0);
    println(processRead(process, "stdout", 64));
    processClose(process);
}}""",
        "true\ntrue\ntrue\nok:hello\n",
        "process spawn pipes environment and exit status",
    )
    require_output(
        f"""global setup(){{}}
global main(){{
    int process = processSpawn({executable}, [str "-c", str "import time; time.sleep(5)"]);
    processSendSignal(process, 15);
    println(processWait(process, 2) < 0);
    processClose(process);
}}""",
        "true\n",
        "process signal delivery",
    )


def test_filesystem_api(temp_root: Path) -> None:
    source = f"""global setup(){{}}
global main(){{
    str root = "{temp_root}";
    str file = root + "/file.txt";
    int handle = filesystemOpen(file, "w");
    println(filesystemWrite(handle, "hello") == 5);
    filesystemClose(handle);
    println(filesystemStat(file));
    int reader = filesystemOpen(file, "r");
    println(filesystemRead(reader, 32));
    filesystemClose(reader);
    filesystemMkdir(root + "/nested");
    filesystemLink(file, root + "/link.txt", true);
    println(filesystemReadLink(root + "/link.txt") == file);
    println(filesystemList(root));
    filesystemRename(file, root + "/renamed.txt");
    filesystemChmod(root + "/renamed.txt", 420);
    filesystemRemove(root + "/link.txt");
    filesystemRemove(root + "/renamed.txt");
    filesystemRemove(root + "/nested");
}}"""
    output, error = run_source(source, str(temp_root / "filesystem.lynx"))
    if error is not None or not output.startswith('true\n{"type":"file"'):
        raise ValidationFailure(
            f"filesystem API failed: error={error.as_string() if error else None}, output={output!r}"
        )
    if (temp_root / "nested").exists() or (temp_root / "renamed.txt").exists():
        raise ValidationFailure("filesystem API did not clean up test entries")
    require_error(
        """global setup(){}
global main(){ filesystemRead(999999, 1); }""",
        "unknown or closed file handle",
        "filesystem closed-handle error",
    )


def test_networking_api(temp_root: Path) -> None:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(3)
    port = listener.getsockname()[1]
    peer_error: list[Exception] = []

    def serve() -> None:
        try:
            connection, _ = listener.accept()
            connection.settimeout(3)
            connection.sendall(b"hello")
            if connection.recv(32) != b"world":
                raise AssertionError("networking client sent unexpected data")
            connection.close()
        except Exception as exc:
            peer_error.append(exc)
        finally:
            listener.close()

    peer = threading.Thread(target=serve)
    peer.start()
    unix_path = temp_root / "lynxer.sock"
    source = f"""global setup(){{}}
global main(){{
    int tcp = networkingOpen("tcp");
    networkingOption(tcp, "keepAlive", 0);
    networkingConnect(tcp, "127.0.0.1", {port});
    println(networkingReceive(tcp, 32));
    println(networkingSend(tcp, "world") == 5);
    networkingShutdown(tcp, "both");
    networkingClose(tcp);
    int udp = networkingOpen("udp");
    networkingBlocking(udp, false);
    networkingBind(udp, "127.0.0.1", 0);
    println(returnLength(networkingAddress(udp)) > 0);
    networkingClose(udp);
    int unixSocket = networkingOpen("unix");
    networkingBind(unixSocket, "{unix_path}");
    println(returnLength(networkingAddress(unixSocket)) > 0);
    networkingClose(unixSocket);
    filesystemRemove("{unix_path}");
    println(returnLength(networkingResolve("localhost", 80)) > 0);
}}"""
    output, error = run_source(source, str(temp_root / "networking.lynx"))
    peer.join(timeout=2)
    if peer.is_alive() or peer_error:
        raise ValidationFailure(f"networking peer failed: {peer_error!r}")
    if error is not None or output != "hello\ntrue\ntrue\ntrue\ntrue\n":
        raise ValidationFailure(
            f"networking API failed: error={error.as_string() if error else None}, output={output!r}"
        )
    require_error(
        """global setup(){}
global main(){ networkingReceive(999999, 1); }""",
        "unknown or closed socket handle",
        "networking closed-handle error",
    )


def test_cli(temp_root: Path) -> None:
    source_path = temp_root / "cli_case.lynx"
    source_path.write_text(
        """global setup(){}
global main(){ println("cli works"); }
""",
        encoding="utf-8",
    )
    run_result = subprocess.run(
        [sys.executable, str(SHELL), str(source_path)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if run_result.returncode != 0 or run_result.stdout != "cli works\n":
        raise ValidationFailure(
            f"CLI source execution failed: rc={run_result.returncode}, "
            f"stdout={run_result.stdout!r}, stderr={run_result.stderr!r}"
        )

    compile_result = subprocess.run(
        [sys.executable, str(SHELL), "--compile", str(source_path)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    bytecode_path = source_path.with_suffix(".lynxc")
    if compile_result.returncode != 0 or not bytecode_path.exists():
        raise ValidationFailure(
            f"CLI bytecode compilation failed: rc={compile_result.returncode}, "
            f"stdout={compile_result.stdout!r}, stderr={compile_result.stderr!r}"
        )


def test_installer_safety() -> None:
    if not _is_elf(str(SHELL)):
        pass  # Python source must not be mistaken for an installable binary.
    if os.path.exists(INSTALL_PATH) and not _is_elf(INSTALL_PATH):
        raise ValidationFailure(
            f"installer: existing {INSTALL_PATH} is not an ELF executable"
        )
    if os.geteuid() != 0 and _matching_pids(INSTALL_PATH):
        raise ValidationFailure("installer: found unexpected Lynxer processes")


def test_existing_fixtures() -> None:
    fixture_pattern = re.compile(r"^test(\d+)\.lynx$")
    fixtures = sorted(
        (
            fixture
            for fixture in (ROOT / "test").iterdir()
            if fixture.is_file() and fixture_pattern.match(fixture.name)
        ),
        key=lambda fixture: int(fixture_pattern.match(fixture.name).group(1))  # type: ignore[union-attr],
    )
    for fixture in fixtures:
        source = fixture.read_text(encoding="utf-8")
        if "forever(" in source:
            print(f"SKIP  existing fixture {fixture.name}: contains unbounded forever()")
            continue
        expected_error = re.search(r"^\s*//\s*EXPECT_ERROR:\s*(.+?)\s*$", source, re.MULTILINE)
        _, error = run_source(source, str(fixture))
        if expected_error:
            fragment = expected_error.group(1)
            if error is None or fragment not in error.as_string():
                raise ValidationFailure(
                    f"fixture {fixture.name} expected error containing {fragment!r}, "
                    f"received {error.as_string() if error else 'no error'}"
                )
        elif error is not None:
            raise ValidationFailure(
                f"existing fixture {fixture.name} failed:\n{error.as_string()}"
            )
        print(f"PASS  {fixture.name}")


TESTS: list[tuple[str, Callable[[], None]]] = [
    ("scalars and operators", test_scalars_and_operators),
    ("lists and tuples", test_lists_and_tuples),
    ("shared aliases", test_shared_aliases),
    ("control flow and functions", test_control_flow_and_functions),
    ("runtime errors", test_runtime_errors),
    ("low-level memory", test_low_level_memory),
    ("installer safety", test_installer_safety),
]


def main() -> int:
    passed = 0
    failed = 0
    with tempfile.TemporaryDirectory(prefix="lynxer-validation-") as directory:
        temp_root = Path(directory)
        tests = TESTS + [
            ("imports", lambda: test_imports(temp_root)),
            ("game stdlib", lambda: test_game_stdlib(temp_root)),
            ("native modules", lambda: test_native_modules(temp_root)),
            ("bytecode", lambda: test_bytecode(temp_root)),
            ("C FFI and native callbacks", test_ffi),
            ("native threads", test_native_threads),
            ("async I/O", lambda: test_async_io(temp_root)),
            ("Linux native syscall", test_linux_syscall),
            ("process API", test_process_api),
            ("filesystem API", lambda: test_filesystem_api(temp_root)),
            ("networking API", lambda: test_networking_api(temp_root)),
            ("CLI", lambda: test_cli(temp_root)),
            ("existing .lynx fixtures", test_existing_fixtures),
        ]
        for name, test in tests:
            try:
                test()
            except Exception as exc:
                failed += 1
                print(f"FAIL  {name}: {exc}", file=sys.stderr)
            else:
                passed += 1
                print(f"PASS  {name}")

    print(f"\nValidation complete: {passed} passed, {failed} failed.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())