# Lynxer — TODO and non-goals

Open work for the standalone C++ implementation in `lynxer/`. The completed
milestone history is in git; this file stays forward-looking. The full
behaviour register is [docs/limitations.md](docs/limitations.md).

## Not planned (will not be implemented)

Permanent non-goals — do **not** start work on these. Kept here so the decision
is visible next to the work that *is* open.

| Feature | Why |
| --- | --- |
| `venv` module | A virtual-environment manager is a Python concept with no equivalent in a standalone runtime. |
| `rawPy`, `rawPyx`, `cleanRawPyxCache`, `embedPy` | Python/Cython embedding. Lynxer does not ship or link a Python runtime. |
| `tkinter`, `tkinterPlus` | No Python GUI toolkit. Any future GUI would be Rust-backed (see `graphics` below). |
| `turtle` | Rust's `turtle` crate has not been maintained since 2019. |
| `http`, `net` modules | Superseded by `network` + `server`. |
| Click/Typer builders (`click*`, `typer*`) | The `cli` module does not depend on Python's Click or Typer. |
| Python runtime introspection (`sys.path`, `addPath`, `prependPath`, `removeFromPath`, `getModules`, `isModuleLoaded`, `getRecursionLimit`, `setRecursionLimit`, `os.getPythonVersion`, `os.getPythonImplementation`) | There is no Python runtime to introspect. |
| Bytecode (`.lynxc`, `--view-bytecode`, `--benchmark-compile`, `--no-cache`) | Removed; `--compile` produces a standalone ELF executable instead. |
| `\x` / `\u` string escapes | Only `\n`, `\r`, `\t`, `\\`, `\"` and `\e` are accepted. |

## Port the original Lynxer surface (`docs-legacy`)

The original interpreter shipped a larger surface than the standalone runtime.
[docs/legacy-surface.md](docs/legacy-surface.md) explains each family and its
replacement; [docs/limitations.md](docs/limitations.md) is the behaviour
register. This is the checkbox backlog for bringing the rest across. Items the
[non-goals table](#not-planned-will-not-be-implemented) currently calls permanent
are marked **reopen** — landing one means deleting its non-goal row and moving
the entry to "Already incorporated".

Status of each unimplemented built-in today:
`<name>() is not supported in Lynxer yet`.

### Language

- [ ] `rawPy { }` / `rawPyx` blocks and their Python interop — **reopen**
      (`rawPy`, `rawPyx`, `cleanRawPyxCache`, `embedPy`). Blocked on embedding a
      Python runtime; everything that depended on `rawPy` (tuple interop,
      `tkinter`, `turtle`) comes with it.
- [ ] Bracket-literal tuple rebinding: the original accepted
      `tuple t = [int 1, int 2]`. Today it fails with
      `value cannot be assigned to type 'tuple'`; use `(int 1, int 2)`,
      `tupleCreate(...)`, or `listToTuple(...)`. See [docs/tuples.md](docs/tuples.md).

### Built-in families

- [x] **Pointers and raw addresses** — `getAddress`, `getAddressValue`,
      `modifyAddressValue`, `functionAddress`, `nativeFunctionAddress`,
      `nativeCall` (addresses are integers; `nativeCall` uses the small integer
      ABI). See the typed blocks/structs/handles above for structured data.
- [x] **Atomics and volatile access** — `atomicLoad`, `atomicStore`,
      `atomicAdd`, `volatileRead`, `volatileWrite` (sequential consistency;
      `atomicAdd` returns the previous value).
- [x] **Native synchronization** — `nativeMutexCreate/Lock/TryLock/Unlock/Close`,
      `nativeConditionCreate/Wait/Notify/NotifyAll/Close`,
      `nativeSemaphoreCreate/Wait/TryWait/Post/Close` over the cooperative
      `nativeThread*` model (blocking waits release the interpreter lock).
- [x] **`memoryProtect`** — change page protection for an allocation
      (POSIX, page-granular).
- [x] **`nativeModule*` handle built-ins** — Load/Name/Function/Constant/Type/
      Error/Dependencies/Close, over the same `lynxer_module_init_v1` loader as
      `importAs`.

### Standard-library module APIs (`docs-legacy/stdlib/*.md`)

- [x] `typing` — legacy surface ported (predicates, conversions, char, string,
      number helpers, `listChunk`).
- [x] `text` — legacy string helpers ported (`indexOf`, `zfill`, `spaces`,
      `wordWrap`, `expandTabs`, `splitFirst`, …).
- [x] `csv` — legacy aliases plus `csvHeaders` / `csvRow` / `dedupCSV` ops.
- [x] `regex` — `countWords`.
- [x] `image` — the 31 legacy ops. Drawing (`drawLine`, `drawRect`,
      `drawRoundedRect`, `drawCircle`, `drawEllipse`, `drawPolygon`, `drawText`,
      `drawTextFont`), filters (`smooth`, `detail`, `edgeEnhance`, `emboss`,
      `findEdges`, `contour`, `medianFilter`, `minFilter`, `maxFilter`,
      `sharpness`, `color`, `composite`), geometry (`contain`, `fit`), the
      histogram operations (`autoContrast`, `equalize`, `solarize`, `posterize`,
      `quantize`), `splitChannels` / `mergeChannels`, `floodFill` and `show`
      (text via `fontdue`; the 3x3 kernels are documented in
      [docs/stdlib/image.md](docs/stdlib/image.md)).
- [ ] `game` — 28 legacy ops. Scenes: `makeScene`, `drawScene`, `updateScene`,
      `addListToScene`. Physics: `makePhysicsEngine`, `updatePhysics`,
      `setPhysicsPlayer`, `jumpPlayer`, `canJump`, `getPlayerVY`. Tilemaps:
      `loadTilemap`, `getTilemapLayer`. Text labels: `makeTextLabel`,
      `setTextLabel`, `setTextLabelColor`, `setTextLabelPos`, `drawTextLabel`,
      `destroyTextLabel`. Animation: `makeAnimatedSprite`, `updateAnimation`.
      Audio: `loadSound`, `playSound`, `loopSound`, `stopSound`,
      `setSoundVolume`, `isSoundPlaying`. Window/output: `setWindowPos`,
      `screenshot`.
- [ ] `server` — 41 legacy ops. Route verbs: `put`, `delete`, `patch`,
      `init`/`run`. Request accessors: `getMethod`, `getPath`, `getUrl`,
      `getBody`, `getHeader`, `getCookie`, `getForm`, `getArg`, `getStatus`,
      `getContentType`, `getRemoteAddr`. Responses: `redirect`, `redirect301`,
      `notFound`, `forbidden`, `serverError`, `methodNotAllowed`, `anyHttp`.
      JSON routes: `jsonRoute`, `jsonGet`, `jsonPost`, `jsonStatus`. Static and
      templates: `staticFiles`, `staticSite`, `serveFile`, `template`,
      `templatePost`, `templateString`, `setTemplateFolder`. Middleware/TLS:
      `cors`, `corsOrigin`, `addGlobalHeader`, `enableRequestLog`, `setDebug`,
      `runHTTPS`, `runSSLAdhoc`.
- [x] `network` — the raw-socket surface the old `net` module had:
      `tcpConnect`, `tcpSend`, `tcpReceive`, `tcpSendReceive`, `tcpClose`,
      `ping`, `isPortOpen`, `getLocalIP` (Rust `std::net`, plaintext TCP only).
- [ ] `cli` — `click*` / `typer*` builders — **reopen**. Today the names are
      rejected and `clickExists()` / `typerExists()` return `false`.
- [ ] `sys` — Python runtime introspection (`addPath`, `prependPath`,
      `removeFromPath`, `getPath`, `getModules`, `isModuleLoaded`,
      `getRecursionLimit`, `setRecursionLimit`) — **reopen**.

### Modules

- [ ] `tkinter` / `tkinterPlus` / `turtle` — **reopen**. Candidate replacement: a
      Rust-backed `graphics` module (see the open decision below).
- [ ] `venv` — **reopen**.
- [ ] `http` / `net` as compatibility shims over `network` + `server` (kept as
      names only; the functionality already exists under the new modules).
- [x] `mathPlus` — merged into `math` (the float `sign` is `signFloat`).

### Toolchain and documentation

- [ ] Bytecode output and `.lynxc` execution (`--view-bytecode`,
      `--benchmark-compile`, `--no-cache`) — **reopen**. Conflicts with the
      current `--compile`-to-ELF design; decide before starting.
- [ ] Absorb the remaining `docs-legacy` reference pages into `docs/`:
      `async.md`, `bytecode.md`, `filesystem.md`, `native-memory.md`,
      `native-modules.md`, `networking.md`, `process.md`, `rawpy.md`,
      `syscalls.md`, and `migration/`. Most of their content already lives in
      `docs/builtins.md` sections; the rest is superseded by
      `docs/native-module-abi.md` and `docs/legacy-surface.md`.

### Already incorporated

- Ownership and borrowing: `varTransfer`, `varTransferMutate`, `varBorrow`,
  `varBorrowMutate`, `varSwapAll`, `varSwapVal`, `varEndBorrow`, `borrowing`,
  `beingBorrowed`, plus `shared` declarations and `unshare()`.
- `async*` family: `asyncRun`, `asyncGather`, `asyncSleep`, `asyncPoll*`,
  timers and wakeups (`await` yields cooperatively).
- Explicit native-module handles: `nativeModuleLoad` / `Name` / `Function` /
  `Constant` / `Type` / `Error` / `Dependencies` / `Close`.
- Native synchronization on cooperative threads: `nativeMutex*`,
  `nativeCondition*`, `nativeSemaphore*`.
- Memory protection, atomics and volatile access: `memoryProtect`,
  `atomicLoad` / `atomicStore` / `atomicAdd`, `volatileRead` / `volatileWrite`.
- Raw addresses and native calls: `getAddress` / `getAddressValue` /
  `modifyAddressValue`, the typed `functionAddress` / `nativeFunctionAddress`,
  and `nativeCall` (small integer ABI).
- Structured memory on integer address handles (the original's pointer
  replacement): typed blocks (`memoryBlock*`, `memoryArray*`, `memoryView*`),
  native structs over a layout string (`nativeStruct*` / `memoryStruct*`,
  `nativeTypeAlignment`), and owned handles (`nativeHandleAllocate` /
  `Address` / `IsAlive` / `Free`).
- `ffi*` family; typed `memory*` accessors (`memoryAllocate`, `memoryRead*` /
  `memoryWrite*`, `memoryReadEndian` / `memoryWriteEndian`, `memoryTypeSize`,
  `memoryTypeAlignment`, `sizeOf`); the `syscall*` family; managed
  `filesystem*` / `process*` / `networking*` / `sound*`; `nativeThread*`.
- Legacy stdlib APIs already ported: `typing`, `text`, `csv`, `regex` (above).

## More named syscalls (platform-compatible)

Every syscall documented by the original (`docs-legacy/syscalls.md`, 89 names) is
implemented via the named dispatcher in `lynxer/builtins.cpp`, and the extended
set below has been added on top of it. A name whose number is missing from the
build's headers fails with `syscall '<name>' is not available on this
architecture` rather than dispatching the wrong table.

**Portability rule.** New wrappers must work on **both** Linux `amd64` and
`aarch64` from the same source: resolve the number in `syscallNumberFor`, guard
each mapping with `#ifdef SYS_<name>` so an older header set degrades to the
"not available on this architecture" error instead of the wrong table, and split
out an architecture-specific name when the raw call differs (the existing
`poll`/`ppoll` split is the model). Prefer a libc wrapper when the raw call is an
unstable ABI (e.g. `clone`/`clone3`).

### Filesystem

- [x] `statx` — already reachable as `syscallGetExtendedFileStatus`.
- [x] `syscallOpenAt2` (`openat2`) — `openat` with a resolve-flags struct.
- [x] `syscallCheckFileAccessAt2` (`faccessat2`) — `faccessat` with flags.
- [x] `syscallCopyFileRange` (`copy_file_range`) — kernel-side copy.
- [x] `syscallFallocateFile` (`fallocate`) — reserve/extend file space.
- [x] `syscallSynchronizeFilesystem` (`syncfs`) — flush one filesystem.

### Processes and threads

- [x] `syscallCreateThread3` (`clone3`) — extensible clone; prefer the libc
      `pthread_create` wrapper outside the thread family.
- [x] `syscallOpenProcessFileDescriptor` (`pidfd_open`) — a pollable process fd.
- [x] `syscallSendSignalToProcessFileDescriptor` (`pidfd_send_signal`).
- [x] `syscallGetThreadAffinity` / `syscallSetThreadAffinity`
      (`sched_getaffinity` / `sched_setaffinity`).
- [x] `syscallGetThreadPriority` / `syscallSetThreadPriority`
      (`getpriority` / `setpriority`).
- [x] `syscallWaitForProcessId` (`waitid`) — the `waitid` sibling of the
      existing `syscallWaitForProcess` (`wait4`).

### Memory

- [x] `syscallLockMemory` / `syscallUnlockMemory` (`mlock` / `munlock`).
- [x] `syscallSynchronizeMemory` (`msync`) — flush an `mmap` region.
- [x] `syscallCreateMemoryFileDescriptor` (`memfd_create`).
- [x] `syscallSetMemoryPolicy` (`mbind`) — NUMA placement; both arches.

### Time

- [x] `syscallGetTimeOfDay` (`gettimeofday`).
- [x] `syscallSleepClock` (`clock_nanosleep`) — the clock-relative sibling of
      `syscallSleep`.
- [x] `syscallCreateTimerFileDescriptor` / `syscallControlTimerFileDescriptor`
      (`timerfd_create` / `timerfd_settime`).

### Signals

- [x] `syscallControlSignal` (`rt_sigaction`).
- [x] `syscallControlSignalMask` (`rt_sigprocmask`).
- [x] `syscallCreateSignalFileDescriptor` (`signalfd`).

### Sockets and event loops

- [x] `syscallSendMessages` / `syscallReceiveMessages` (`sendmmsg` / `recvmmsg`).
- [x] `syscallAcceptConnection4` (`accept4`) — `accept` with flags.
- [x] `syscallWaitForEvents2` (`epoll_pwait2`) — nanosecond `epoll` timeout.
- [x] `syscallCreateEventFileDescriptor` (`eventfd`) — for poll/wakeup plumbing.

### System information and control

- [x] `prctl` — already reachable as `syscallControlProcess`.
- [x] `syscallGetCapabilities` / `syscallSetCapabilities`
      (`capget` / `capset`).
- [x] `syscallGetSystemTimes` (`times`).

### Stretch (higher complexity)

- [ ] io_uring (`syscallSetupIoUring`, `syscallEnterIoUring`,
      `syscallRegisterIoUring`) — present on both arches; large surface, so land
      it only with a dedicated fixture.
- [ ] Landlock / seccomp sandboxing (`syscallCreateLandlockRuleset`,
      `syscallControlSeccomp`) — both arches, but security-sensitive and easy to
      misuse; decide the exposure first.

## Open decisions

Work that needs a decision before it can be built:

- [ ] **`ffi*` calling convention.** `ffiCall` has to describe and perform
  arbitrary native calls. `libffi` is the usual answer and is a **new build
  dependency** for the interpreter; hand-rolling covers only a few fixed
  signatures. It is also the largest security surface, since it turns a Lynxer
  program into arbitrary native code. Decide: take the dependency, hand-roll
  fixed shapes, or move the family to "not planned".
- [ ] **`async*` semantics.** The family is implemented (`asyncRun`,
  `asyncGather`, `asyncSleep`, the `asyncPoll*` set, timers and wakeups) and
  `await` yields cooperatively, but there is no `async` language support.
  Decide whether to keep it as-is, expand it, or freeze the surface.
- [ ] **`nativeThread*` interleaving.** Threads are cooperative: a worker cannot
  make progress while the main body is running, which is the opposite of a
  pre-emptive runtime. Decide whether real interleaving is needed.
- [ ] **`graphics` module.** A Rust `iced`-backed GUI replacement for the
  not-planned `tkinter` / `turtle` surface — decide whether to build it.
- [ ] **`tui` real backend.** The API surface is complete but the backend is a
  placeholder: rendering draws fixed output, the prompt operations return empty
  defaults, and the stateful families keep no state. Decide whether to build a
  real terminal UI.

## Ground rules

- Keep everything under `lynxer/`; documentation in `docs/`, the website in
  `site/`.
- C++17 and the standard library only, unless a native dependency is explicitly
  chosen. The Rust backends are optional — a missing `cargo` only skips them.
- Prefer explicit, source-located errors over partial support.
- Add a focused `.lynx` fixture under `lynxer/examples/` for every user-visible
  feature, with a sibling `.expected`, and keep `make testLynxer` green.
- Regenerate the website with `python3 site/build.py` after editing `docs/`.
