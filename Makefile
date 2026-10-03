PYTHON   ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)

# --- Lynxer: the standalone C++ implementation -----------------------------
# Every path is repo-root relative.
LYNXER_DIR := lynxer
LYNXER_TARGET := $(LYNXER_DIR)/lynxer
LYNXER_SOURCES := $(addprefix $(LYNXER_DIR)/,main.cpp shell.cpp lexer.cpp runtime.cpp types.cpp builtins.cpp ops.cpp ast.cpp optimizer.cpp formatter.cpp parser.cpp config.cpp bundle.cpp interrupt.cpp native_value.cpp exports.cpp embed.cpp platform.cpp)
LYNXER_OBJECTS := $(LYNXER_SOURCES:.cpp=.o)
LYNXER_OBJECTS_ARM64 := $(LYNXER_SOURCES:.cpp=.o-arm64)
LYNXER_HEADERS := $(wildcard $(LYNXER_DIR)/*.hpp)
LYNXER_CXX ?= c++

# --- Host platform -----------------------------------------------------------
# `OS` is `Windows_NT` on Windows and normally unset elsewhere, which is not
# enough on its own to know the host. Detect it once and key every platform
# decision off `LYNXER_HOST_OS` / `LYNXER_ON_*` rather than guessing.
ifeq ($(OS),Windows_NT)
LYNXER_HOST_OS := windows
else
LYNXER_HOST_OS := $(shell uname -s 2>/dev/null)
ifeq ($(strip $(LYNXER_HOST_OS)),)
LYNXER_HOST_OS := unknown
endif
# MSYS2/Git and Cygwin set `OS` only sometimes; their `uname` names them.
ifneq ($(filter MINGW% MSYS% CYGWIN%,$(LYNXER_HOST_OS)),)
LYNXER_HOST_OS := windows
endif
endif
LYNXER_ON_WINDOWS := $(if $(filter windows,$(LYNXER_HOST_OS)),1,)
LYNXER_ON_MACOS := $(if $(filter Darwin macos,$(LYNXER_HOST_OS)),1,)

# The interpreter is `lynxer` on POSIX and `lynxer.exe` on Windows. MinGW appends
# `.exe` on its own, so name it explicitly to keep make's prerequisites and
# `$(CLYX)` pointing at the file that actually exists.
LYNXER_TARGET := $(LYNXER_DIR)/lynxer$(if $(LYNXER_ON_WINDOWS),.exe,)

# Platform compile flags and link libraries.
#
# -fPIC, not -fPIE: the same objects are linked into the PIE interpreter and
# into the shared embedding runtime `liblynxer.so`. `-ftls-model=global-dynamic`
# keeps the thread_local state (error buffers, native callbacks) valid in a
# shared object, where the default local-exec model is rejected by the linker.
# Neither flag applies on Windows, where the ELF shared runtime is not built yet.
#
# `-ldl` exists on Linux and the BSDs but not on macOS (`dlopen` lives in
# libSystem) or Windows, so it is added only off macOS.
ifeq ($(LYNXER_ON_WINDOWS),1)
LYNXER_PLATFORM_FLAGS :=
LYNXER_PLATFORM_LIBS := -lpthread -lm -lffi
LYNXER_BUILD_SHARED := 0
else
LYNXER_PLATFORM_FLAGS := -fPIC -ftls-model=global-dynamic
ifeq ($(LYNXER_ON_MACOS),1)
LYNXER_PLATFORM_LIBS := -lpthread -lm -lffi
# `liblynxer.so` links with an ELF version script and `-soname`, so the embedding
# runtime is built on Linux only for now.
LYNXER_BUILD_SHARED := 0
else
LYNXER_PLATFORM_LIBS := -lpthread -ldl -lm -lffi
LYNXER_BUILD_SHARED := 1
endif
endif

LYNXER_CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic $(LYNXER_PLATFORM_FLAGS)

# Modules a Windows build must skip because they have no Windows backend yet.
# `sys` is built on Linux system calls (and the syscall built-ins are Linux-only);
# `cli`, `debug`, `os` and `path` use POSIX headers MinGW does not provide.
# Porting each is tracked in docs/windows.md and todo.md.
ifeq ($(LYNXER_ON_WINDOWS),1)
LYNXER_WINDOWS_SKIP_MODULES := sys cli debug os path js multiprocessing
else
LYNXER_WINDOWS_SKIP_MODULES :=
endif

# Native (C++) stdlib modules: every lynxer/stdlib/<name>.cpp -> <name>.so.
LYNXER_ALL_NATIVE_SOURCES := $(wildcard $(LYNXER_DIR)/stdlib/*.cpp)
LYNXER_NATIVE_SOURCES := $(filter-out $(addprefix $(LYNXER_DIR)/stdlib/,$(addsuffix .cpp,$(LYNXER_WINDOWS_SKIP_MODULES))),$(LYNXER_ALL_NATIVE_SOURCES))
LYNXER_NATIVE_MODULES := $(LYNXER_NATIVE_SOURCES:.cpp=.so)

# Rust backends: self-contained cdylibs that the interpreter dlopens directly.
LYNXER_RUST_DIR := $(LYNXER_DIR)/rust
LYNXER_RUST_TARGET_DIR := $(LYNXER_DIR)/build/rust
# Cargo names a `cdylib` `lib<name>.so` on Linux and the BSDs, `lib<name>.dylib`
# on macOS, and `<name>.dll` (no `lib` prefix) on Windows.
ifeq ($(LYNXER_ON_WINDOWS),1)
LYNXER_CDYLIB_PREFIX :=
LYNXER_CDYLIB_SUFFIX := .dll
else ifeq ($(LYNXER_ON_MACOS),1)
LYNXER_CDYLIB_PREFIX := lib
LYNXER_CDYLIB_SUFFIX := .dylib
else
LYNXER_CDYLIB_PREFIX := lib
LYNXER_CDYLIB_SUFFIX := .so
endif
LYNXER_RUST_MANIFEST := $(LYNXER_RUST_DIR)/Cargo.toml
LYNXER_RUST_SOURCES := $(wildcard $(LYNXER_RUST_DIR)/*/src/*.rs) \
                        $(wildcard $(LYNXER_RUST_DIR)/*/Cargo.toml) \
                        $(LYNXER_RUST_MANIFEST) $(LYNXER_RUST_DIR)/Cargo.lock
LYNXER_RUST_MODULE_NAMES := encoding crypto compress game graphics image ini json lua network re regex server sound sqldb text toml tui uuid watch xml yaml

# `watch` has only Linux (inotify) and macOS/BSD (kqueue) backends; a Windows
# build needs a `ReadDirectoryChangesW` backend (see docs/windows.md).
ifeq ($(LYNXER_ON_WINDOWS),1)
LYNXER_RUST_MODULE_NAMES := $(filter-out watch,$(LYNXER_RUST_MODULE_NAMES))
endif
LYNXER_RUST_MODULES := $(LYNXER_RUST_MODULE_NAMES:%=$(LYNXER_DIR)/stdlib/%.so)

# The native-call engine is not a stdlib module: it is a Rust staticlib linked
# directly into the interpreter, and cargo is required to build it.
LYNXER_FFI_ABI_HEADER := $(LYNXER_DIR)/ffi_abi.h
LYNXER_FFI_STATICLIB := $(LYNXER_RUST_TARGET_DIR)/release/liblynxer_ffi.a
# `rustc --print native-static-libs` records the native libraries the staticlib
# needs at the final link: Windows system DLL imports (ntdll, ws2_32, userenv,
# ...), `libffi`, and the C runtime. A Rust staticlib does not carry its
# dependencies' link directives, so the C++ link has to add them; capturing the
# exact list here keeps that correct on every platform.
LYNXER_FFI_BUILD_LOG := $(LYNXER_RUST_TARGET_DIR)/release/lynxer_ffi-build.log
LYNXER_FFI_NATIVE_LIBS := $(LYNXER_RUST_TARGET_DIR)/release/lynxer_ffi.native-libs
# A Rust `staticlib` does not carry its dependencies' link directives. On Windows
# the archive needs the system DLL import libraries (`ntdll`, `ws2_32`,
# `userenv`, ...) that nothing else supplies, so add what
# `rustc --print native-static-libs` reported. On POSIX the C++ driver and
# `LYNXER_PLATFORM_LIBS` already cover it, and naming `-lc` fails on hosts with
# no `libc.so` development symlink, so the captured list is not used there.
# Recursive (`=`) so the `cat` runs when the link recipe expands, after the
# archive and its `.native-libs` file have been built.
ifeq ($(LYNXER_ON_WINDOWS),1)
LYNXER_FFI_LINK_LIBS = $(shell cat $(LYNXER_FFI_NATIVE_LIBS) 2>/dev/null)
else
LYNXER_FFI_LINK_LIBS =
endif

# The embedding runtime and its public C header. `liblynxer.so` is the core
# interpreter (minus `main.cpp`) as a shared library; libraries emitted by
# `lynxer --emit-library` link against it. Only the `lynxer_embed_*` entry
# points are exported (see the version script).
LYNXER_SHARED := $(LYNXER_DIR)/liblynxer.so
ifeq ($(LYNXER_BUILD_SHARED),1)
LYNXER_SHARED_BUILT := $(LYNXER_SHARED)
LYNXER_EMIT_TEST := testLynxerEmit
else
LYNXER_SHARED_BUILT :=
LYNXER_EMIT_TEST :=
endif
LYNXER_SHARED_OBJECTS := $(filter-out $(LYNXER_DIR)/main.o,$(LYNXER_OBJECTS))
LYNXER_SHARED_VERSION_SCRIPT := $(LYNXER_DIR)/liblynxer.map
LYNXER_PUBLIC_HEADERS := $(LYNXER_DIR)/lynxer.h $(LYNXER_FFI_ABI_HEADER)

# The Rust toolchain is required, not optional: every Rust backend is built by
# cargo, and the interpreter links the Rust native-call engine. A missing
# toolchain or a failed `cargo build` is a hard error -- Lynxer always builds
# complete, never a subset of itself.
LYNXER_CARGO ?= cargo

# Every stdlib backend, C++ and Rust. Nothing here is conditional.
LYNXER_NATIVE_BUILT := $(LYNXER_NATIVE_MODULES) $(LYNXER_RUST_MODULES)

# Lynxer suite fixtures and static contract check.
LYNXER_CONTRACT_CHECK := $(LYNXER_DIR)/scripts/check_module_contracts.py
LYNXER_CONDITION_FIXTURE := $(LYNXER_DIR)/examples/conditions.lynx
LYNXER_LOOP_FIXTURE := $(LYNXER_DIR)/examples/loops.lynx
LYNXER_ERROR_FIXTURE := $(LYNXER_DIR)/examples/control_flow_error.lynx
LYNXER_COMMENT_FIXTURE := $(LYNXER_DIR)/examples/comments.lynx
LYNXER_SETUP_ERROR_FIXTURE := $(LYNXER_DIR)/examples/missing_setup.lynx
# A run-time failure raised inside an imported module: the diagnostic must name
# the module file, not the importing program.
LYNXER_MODULE_ERROR_FIXTURE := $(LYNXER_DIR)/examples/module_error.lynx
LYNXER_MODULE_ERROR_LIB := $(LYNXER_DIR)/examples/module_error_lib.lynx
LYNXER_INPUTLN_FIXTURE := $(LYNXER_DIR)/examples/inputln.lynx
LYNXER_MILESTONE4_FIXTURE := $(LYNXER_DIR)/examples/milestone4.lynx
LYNXER_MILESTONE4_PATTERN_FIXTURE := $(LYNXER_DIR)/examples/milestone4_patterns.lynx
LYNXER_MILESTONE5_FIXTURE := $(LYNXER_DIR)/examples/milestone5.lynx
LYNXER_MILESTONE5_CODEBLOCK_FIXTURE := $(LYNXER_DIR)/examples/milestone5_codeblocks.lynx
LYNXER_MILESTONE6_FIXTURE := $(LYNXER_DIR)/examples/milestone6_module.lynx
LYNXER_MILESTONE6_MATH_FIXTURE := $(LYNXER_DIR)/examples/milestone6_math_native.lynx
# A module imported by a path with a directory component, resolved relative to
# the importing program's own directory: import("testPath/example.lynx").
LYNXER_PATH_IMPORT_FIXTURE := $(LYNXER_DIR)/examples/testPathImport.lynx
LYNXER_MACRO_FIXTURE := $(LYNXER_DIR)/examples/macros.lynx
LYNXER_NATIVE_STDLIB_FIXTURE := $(LYNXER_DIR)/examples/native_stdlibs.lynx
# The program's own command line (sys.argv). Run with extra arguments so the
# fixture can assert the script path is entry 0 and the rest follow it.
LYNXER_PROGRAM_ARGS_FIXTURE := $(LYNXER_DIR)/examples/program_args.lynx
LYNXER_SIGNATURE_SOURCE := $(LYNXER_DIR)/examples/native_signatures.cpp
LYNXER_SIGNATURE_MODULE := $(LYNXER_DIR)/examples/native_signatures.so
LYNXER_SIGNATURE_FIXTURE := $(LYNXER_DIR)/examples/native_signatures.lynx
# Every lynxer/examples/stdlib_<name>.lynx must ship a sibling .expected file.
# The sound fixture is split out: although the backend tolerates a missing
# output device, several of its assertions (playback, stop, per-handle volume)
# only hold on a host with a real ALSA card. Hosts without one — CI containers,
# headless boxes — skip it and say so instead of failing.
HAVE_AUDIO := $(if $(wildcard /dev/snd/controlC*),1,)
LYNXER_SOUND_FIXTURE := $(LYNXER_DIR)/examples/stdlib_sound.lynx
# The server TLS fixture completes a real handshake, so it needs `curl` to make
# the request and `openssl` to mint a throwaway certificate. Both are on CI
# runners and normal dev boxes; when either is missing the fixture is skipped
# and says so, the way `stdlib_sound` is skipped without an audio device.
HAVE_TLS_TOOLS := $(if $(and $(shell command -v curl 2>/dev/null),$(shell command -v openssl 2>/dev/null)),1,)
LYNXER_SERVER_TLS_FIXTURE := $(LYNXER_DIR)/examples/stdlib_server_tls.lynx
LYNXER_GUI_FIXTURES := $(LYNXER_DIR)/examples/gui_graphics_window.lynx \
	$(LYNXER_DIR)/examples/gui_game_window.lynx
# Display/audio tests. CI runners have neither a display nor an audio device, and
# the graphics backend crashes without a display, so the workflows pass
# LYNXER_SKIP_DISPLAY=1 to drop the fixtures that need one.
LYNXER_SKIP_DISPLAY ?= 0
ifeq ($(LYNXER_SKIP_DISPLAY),1)
LYNXER_DISPLAY_FIXTURES := stdlib_game stdlib_graphics stdlib_sound
else
LYNXER_DISPLAY_FIXTURES :=
endif
LYNXER_DISPLAY_FIXTURE_FILES := $(LYNXER_DISPLAY_FIXTURES:%=$(LYNXER_DIR)/examples/%.lynx)
# The tui fixture is split out like sound: its prompts read stdin, which the
# generic loop does not feed, so it runs from a dedicated target with piped
# input (see below).
LYNXER_TUI_FIXTURE := $(LYNXER_DIR)/examples/stdlib_tui.lynx
LYNXER_STDLIB_FIXTURES := $(filter-out $(LYNXER_SOUND_FIXTURE) $(LYNXER_TUI_FIXTURE) $(LYNXER_SERVER_TLS_FIXTURE) $(LYNXER_DISPLAY_FIXTURE_FILES),$(wildcard $(LYNXER_DIR)/examples/stdlib_*.lynx))
# A module that is not built on Windows has no fixture to run; skip it instead
# of failing on a missing import (`watch` is the Rust backend, see above).
ifeq ($(LYNXER_ON_WINDOWS),1)
LYNXER_WINDOWS_SKIP_FIXTURES := $(LYNXER_WINDOWS_SKIP_MODULES) watch
LYNXER_STDLIB_FIXTURES := $(filter-out $(addprefix $(LYNXER_DIR)/examples/stdlib_,$(addsuffix .lynx,$(LYNXER_WINDOWS_SKIP_FIXTURES))),$(LYNXER_STDLIB_FIXTURES))
endif
ifeq ($(HAVE_AUDIO),1)
LYNXER_AUDIO_FIXTURES := $(filter-out $(LYNXER_DISPLAY_FIXTURE_FILES),$(LYNXER_SOUND_FIXTURE))
else
LYNXER_AUDIO_FIXTURES :=
endif
# Built-in-family fixtures (lynxer/examples/builtin_<name>.lynx) do too.
LYNXER_MILESTONE7_NEW_FIXTURES := $(LYNXER_DIR)/examples/builtin_async.lynx $(LYNXER_DIR)/examples/builtin_ffi.lynx $(LYNXER_DIR)/examples/builtin_ffi_errors.lynx $(LYNXER_DIR)/examples/builtin_bytes.lynx
# Ownership/borrowing built-ins: moves, borrows, swaps and their error paths.
LYNXER_OWNERSHIP_FIXTURES := $(LYNXER_DIR)/examples/ownership.lynx
# Tuple literals: the canonical `()` form plus the legacy bracket literal
# `[int 1, int 2]`, which a `tuple` target coerces to a tuple.
LYNXER_TUPLE_FIXTURES := $(LYNXER_DIR)/examples/tuple_rebinding.lynx
# Range `for` loop: for (int i = start (.. | ..=) end [.. step]).
LYNXER_RANGE_FIXTURES := $(LYNXER_DIR)/examples/range_for.lynx
# Low-level fixtures: native-memory typed/endian access and the portable named
# syscalls. They assert only host-independent behaviour, so the same expected
# output holds on amd64 and arm64, and both CI jobs run them.
LYNXER_LOWLEVEL_FIXTURES := $(LYNXER_DIR)/examples/lowlevel_memory.lynx \
	$(LYNXER_DIR)/examples/lowlevel_syscalls.lynx \
	$(LYNXER_DIR)/examples/lowlevel_arch.lynx \
	$(LYNXER_DIR)/examples/native_memory_structs.lynx \
	$(LYNXER_DIR)/examples/memory_atomics.lynx \
	$(LYNXER_DIR)/examples/raw_addresses.lynx \
	$(LYNXER_DIR)/examples/native_sync.lynx \
	$(LYNXER_DIR)/examples/native_sync_threads.lynx \
	$(LYNXER_DIR)/examples/native_module_api.lynx \
	$(LYNXER_DIR)/examples/syscall_extended.lynx \
	$(LYNXER_DIR)/examples/syscall_io_uring.lynx \
	$(LYNXER_DIR)/examples/language_fields.lynx
# Architecture-specific syscall fixtures. They stay out of testLynxer because
# each drives syscalls that exist on only one target: the AMD64 job runs
# amd64Syscalls.lynx (poll/epoll_wait, and the ARM64-only wrappers must be
# rejected), the ARM64 job runs arm64Syscalls.lynx (ppoll/epoll_pwait). Each is
# diffed against its .expected file by a target that refuses to run on the
# wrong host architecture.
LYNXER_AMD64_SYSCALL_FIXTURE := $(LYNXER_DIR)/examples/amd64Syscalls.lynx
LYNXER_ARM64_SYSCALL_FIXTURE := $(LYNXER_DIR)/examples/arm64Syscalls.lynx
# Compatibility fixture for the deprecated symbolic operator spellings. It is
# diffed like a stdlib fixture and also compiled, so the old spellings keep
# working through both the interpreter and --compile.
LYNXER_DEPRECATED_FIXTURES := $(LYNXER_DIR)/examples/deprecated_operators.lynx
# Single self-checking test that exercises every stdlib module at once.
LYNXER_STDLIB_TEST_ALL := $(LYNXER_DIR)/examples/stdlibTestAll.lynx
# AST optimizer: fixture plus its expected stdout+stderr, so an optimized run,
# a --no-opt run and the report all have something to compare against.
LYNXER_OPTIMIZER_FIXTURE := $(LYNXER_DIR)/examples/optimizer.lynx
LYNXER_OPTIMIZER_EXPECTED := $(LYNXER_DIR)/examples/optimizer.expected
LYNXER_OPTIMIZER_DEPRECATED_FIXTURE := $(LYNXER_DIR)/examples/optimizer_deprecated.lynx
# Formatter fixture: a deliberately messy source file and its canonical form.
LYNXER_FORMATTER_INPUT := $(LYNXER_DIR)/examples/formatter_input.lynx
LYNXER_FORMATTER_EXPECTED := $(LYNXER_DIR)/examples/formatter_expected.lynx
LYNXER_LIST_STDLIB_MODULES := cli colorlib compress crypto csv debug encoding fileIO game graphics image js json lua math \
	multiprocessing network os path random re regex server shell sound sqldb sys text time toml tui turtle typing uuid watch xml yaml
# Import-parity fixtures (interpreted vs compiled). The sound one needs a device.
LYNXER_PARITY_FIXTURES := native_stdlibs native_aggregate milestone6_module testPathImport macros milestone6_math_native stdlib_encoding stdlib_crypto stdlib_compress stdlib_json stdlib_uuid \
	stdlib_toml stdlib_ini stdlib_xml stdlib_yaml stdlib_watch \
	stdlib_re stdlib_path stdlib_text stdlib_game stdlib_graphics stdlib_image stdlib_lua stdlib_sqldb stdlib_tui deprecated_operators optimizer \
	lowlevel_memory lowlevel_syscalls lowlevel_arch language_fields ownership \
	range_for
ifeq ($(HAVE_AUDIO),1)
LYNXER_PARITY_FIXTURES += stdlib_sound
endif
LYNXER_PARITY_FIXTURES := $(filter-out $(LYNXER_DISPLAY_FIXTURES),$(LYNXER_PARITY_FIXTURES))

# --- Checks that cannot run on Windows -------------------------------------
# `sys`, `cli`, `debug`, `os`, `path`, `js` and `multiprocessing` are not built
# there (LYNXER_WINDOWS_SKIP_MODULES), and the named syscalls and the native
# memory/sync surface have no Windows backend yet, so the checks that use them
# are skipped. See docs/windows.md.
ifeq ($(LYNXER_ON_WINDOWS),1)
LYNXER_EXIT_FIXTURES :=
LYNXER_EXIT_THREAD_FIXTURES :=
LYNXER_NATIVE_STDLIB_FIXTURE :=
LYNXER_STDLIB_TEST_ALL :=
LYNXER_LOWLEVEL_FIXTURES :=
LYNXER_PARITY_FIXTURES := $(filter-out native_stdlibs stdlib_path stdlib_watch lowlevel_memory lowlevel_syscalls lowlevel_arch,$(LYNXER_PARITY_FIXTURES))
else
LYNXER_EXIT_FIXTURES := sys_exit cli_exit
LYNXER_EXIT_THREAD_FIXTURES := sys_exit_thread sys_exit_worker
endif

# Recipe shorthands: the interpreter, and the temp-file prefix for the suite.
CLYX := ./$(LYNXER_TARGET)
CLYX_TMP := $(LYNXER_DIR)/.lynxer
# Throwaway install prefix for the `--install` gate: `lynxer --install` refuses
# to touch the real /usr here, so the whole flow runs without root.
LYNXER_INSTALL_PREFIX := $(CLYX_TMP)_install_prefix
LYNXER_INSTALLED_BIN := $(LYNXER_INSTALL_PREFIX)/bin/$(notdir $(LYNXER_TARGET))
# Canonical syscall architecture of this host. The architecture-agnostic syscall
# fixtures carry a __ARCH__ token (syscalls("__ARCH__") plus __ARCH__.syscall*);
# it is replaced with this word before they run, so one source serves both CI
# jobs.
SYSCALL_ARCH := $(shell uname -m | sed -e 's/^x86_64$$/amd64/' -e 's/^aarch64$$/arm64/')

.PHONY: all cargo lynxerToolchain build buildAll buildLynxer buildLynxerArm64 test testLynxer testLynxerGui testLynxerInstall testLynxerAmd64Syscalls testLynxerArm64Syscalls check clean cleanLynxer cleanAll help

test: testLynxer

# Lint the syntax showcase, then run the suite.
check: testLynxer
	@for file in syntax.lynx; do \
		$(CLYX) --lint "$$file" >/dev/null || exit $$?; \
	done
	@echo "✓ Lynxer checks passed."

# Conventional alias for a full build.
all: build

# The interpreter, the native (C++) stdlib modules, and the Rust backends.
build: buildLynxer
	@echo "✓ Full build complete: $(LYNXER_TARGET)"

buildAll: build


# ---------------------------------------------------------------------------
# Lynxer: the C++ interpreter, its native (C++) stdlib modules, and the Rust
# stdlib backends. All paths are repo-root relative.
# ---------------------------------------------------------------------------

# The toolchain gate. It is the first prerequisite of every entry point that
# needs Rust, so a missing cargo fails at once with one clear message instead of
# halfway through the C++ compile. It is a phony target (always run) and never a
# prerequisite of a real file, so it cannot force a relink.
lynxerToolchain:
	@command -v $(LYNXER_CARGO) >/dev/null 2>&1 || { \
	echo "lynxer: cargo is required to build Lynxer, but it was not found on PATH."; \
	echo "lynxer: install a Rust toolchain (e.g. rustup) and run make again."; \
	exit 1; }

# Binary plus every stdlib module, C++ and Rust alike.
buildLynxer: lynxerToolchain $(LYNXER_TARGET) $(LYNXER_NATIVE_BUILT) $(LYNXER_SHARED_BUILT) sdk
	@echo "✓ Lynxer build complete: $(LYNXER_TARGET)"

# The embedding SDK staged for consumers: the shared runtime plus the public
# headers, under lynxer/build/sdk/{include,lib}. Libraries built by
# `--emit-library` compile against this. Also installed by `--install`.
LYNXER_SDK_DIR := $(LYNXER_DIR)/build/sdk
ifeq ($(LYNXER_BUILD_SHARED),1)
sdk: lynxerToolchain $(LYNXER_SHARED)
	@mkdir -p $(LYNXER_SDK_DIR)/include $(LYNXER_SDK_DIR)/lib
	@cp $(LYNXER_PUBLIC_HEADERS) $(LYNXER_SDK_DIR)/include/
	@cp $(LYNXER_SHARED) $(LYNXER_SDK_DIR)/lib/
	@echo "✓ Lynxer embedding SDK staged in $(LYNXER_SDK_DIR)"
else
sdk:
	@echo "lynxer: the embedding SDK is not built on Windows yet (see docs/windows.md)"
endif

# Bob, the Lynxer package manager, lives in Bob/ as its own component with its
# own Makefile; it is deliberately kept separate from the interpreter and is not
# part of `buildLynxer`. Build or clean it through this convenience target.
BOB_DIR := Bob
buildBob: lynxerToolchain
	@$(MAKE) -C $(BOB_DIR) build

cleanBob:
	@$(MAKE) -C $(BOB_DIR) clean
# ARM64 (aarch64) binary. Requires aarch64-linux-gnu-g++ installed.
buildLynxerArm64: lynxerToolchain $(LYNXER_TARGET)-arm64 $(LYNXER_NATIVE_BUILT)
	@echo "✓ Lynxer ARM64 build complete: $(LYNXER_TARGET)-arm64"

# The Rust backends and the native-call engine on their own. Cargo is required:
# a missing toolchain or a failed build is a hard error, never a silent skip.
cargo: lynxerToolchain $(LYNXER_RUST_MODULES) $(LYNXER_FFI_STATICLIB)
	@echo "✓ Lynxer Rust backends ready ($(LYNXER_RUST_TARGET_DIR))"

# The interpreter links the native-call engine, so `cargo` is a hard build
# dependency: `-lpthread -ldl -lm` are the Rust runtime's, and `-lffi` the
# engine's (a staticlib does not carry its dependencies' link directives).
$(LYNXER_TARGET): $(LYNXER_OBJECTS) $(LYNXER_FFI_STATICLIB)
	$(LYNXER_CXX) $(LYNXER_CXXFLAGS) $(LYNXER_OBJECTS) $(LYNXER_FFI_STATICLIB) -o $@ \
	    $(LYNXER_PLATFORM_LIBS) $(LYNXER_FFI_LINK_LIBS)

# The embedding runtime: the same objects without `main.o`, linked as a shared
# library so `--emit-library` shims can `-llynxer`. The version script hides
# every symbol except the `lynxer_embed_*` entry points.
$(LYNXER_SHARED): $(LYNXER_SHARED_OBJECTS) $(LYNXER_FFI_STATICLIB) $(LYNXER_SHARED_VERSION_SCRIPT)
	$(LYNXER_CXX) $(LYNXER_CXXFLAGS) -shared $(LYNXER_SHARED_OBJECTS) $(LYNXER_FFI_STATICLIB) -o $@ \
	    -Wl,--version-script=$(LYNXER_SHARED_VERSION_SCRIPT) -Wl,-soname,liblynxer.so \
	    $(LYNXER_PLATFORM_LIBS) $(LYNXER_FFI_LINK_LIBS)

# The ARM64 interpreter links the same Rust native-call engine, so the host must
# be able to produce an aarch64 staticlib. Native aarch64 (the ARM CI runner) is
# the supported path: an amd64 cargo builds an amd64 engine, which cannot be
# linked into an arm64 binary. Fail with that explanation rather than a
# confusing linker error.
$(LYNXER_TARGET)-arm64: $(LYNXER_OBJECTS_ARM64) $(LYNXER_FFI_STATICLIB)
	@command -v aarch64-linux-gnu-g++ >/dev/null || { echo "error: aarch64-linux-gnu-g++ not found"; exit 1; }
	@if [ "$$(uname -m)" != "aarch64" ]; then \
	echo "error: cannot link the aarch64 native-call engine from $$(uname -m)."; \
	echo "hint: build on an aarch64 host, or point LYNXER_FFI_STATICLIB at an aarch64 liblynxer_ffi.a."; \
	exit 1; fi
	@aarch64-linux-gnu-g++ $(LYNXER_CXXFLAGS) $(filter %.o-arm64,$^) $(LYNXER_FFI_STATICLIB) -o $@ -static-libstdc++ -static-libgcc -lpthread -ldl -lm -lffi

$(LYNXER_DIR)/%.o: $(LYNXER_DIR)/%.cpp $(LYNXER_HEADERS) $(LYNXER_FFI_ABI_HEADER)
	$(LYNXER_CXX) $(LYNXER_CXXFLAGS) -c $< -o $@

$(LYNXER_DIR)/%.o-arm64: $(LYNXER_DIR)/%.cpp $(LYNXER_HEADERS) $(LYNXER_FFI_ABI_HEADER)
	@command -v aarch64-linux-gnu-g++ >/dev/null || { echo "error: aarch64-linux-gnu-g++ not found"; exit 1; }
	@aarch64-linux-gnu-g++ $(LYNXER_CXXFLAGS) -c $< -o $@

# Remaining C++ stdlib modules, compiled to lynxer/stdlib/<name>.so.
$(LYNXER_DIR)/stdlib/%.so: $(LYNXER_DIR)/stdlib/%.cpp
	$(LYNXER_CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $< -o $@

# Rust backends: self-contained cdylibs exporting lynxer_module_init_v1 and
# their ops, copied to lynxer/stdlib/<name>.so for the interpreter to dlopen.
# Cargo is required for each of them; a failure stops the whole build.
$(LYNXER_RUST_TARGET_DIR)/release/$(LYNXER_CDYLIB_PREFIX)lynxer_%$(LYNXER_CDYLIB_SUFFIX): $(LYNXER_RUST_SOURCES)
	@command -v $(LYNXER_CARGO) >/dev/null 2>&1 || { echo "lynxer: cargo is required to build the Rust backend lynxer_$*"; exit 1; }
	RUSTFLAGS="-C relocation-model=pic" $(LYNXER_CARGO) build -p lynxer_$* --release \
	    --manifest-path $(LYNXER_RUST_MANIFEST) --target-dir $(LYNXER_RUST_TARGET_DIR)

define LYNXER_RUST_MODULE_RULE
$(LYNXER_DIR)/stdlib/$(1).so: $(LYNXER_RUST_TARGET_DIR)/release/$(LYNXER_CDYLIB_PREFIX)lynxer_$(1)$(LYNXER_CDYLIB_SUFFIX)
	@test -f $$< || { echo "lynxer: cargo did not produce $$<"; ls $(LYNXER_RUST_TARGET_DIR)/release/ | grep -i 'lynxer_$(1)' || true; exit 1; }
	cp $$< $$@
endef
$(foreach name,$(LYNXER_RUST_MODULE_NAMES),$(eval $(call LYNXER_RUST_MODULE_RULE,$(name))))

# The native-call engine: a Rust staticlib linked into the interpreter, not a
# stdlib module. It is required, so a missing cargo is a hard error. `cargo
# rustc --print native-static-libs` both produces the archive and reports the
# native libraries the final C++ link must add (see LYNXER_FFI_NATIVE_LIBS).
$(LYNXER_FFI_STATICLIB): $(LYNXER_RUST_SOURCES) $(LYNXER_FFI_ABI_HEADER)
	@command -v $(LYNXER_CARGO) >/dev/null 2>&1 || { echo "lynxer: cargo is required to build the native-call engine"; exit 1; }
	@mkdir -p $(dir $(LYNXER_FFI_BUILD_LOG))
	@RUSTFLAGS="-C relocation-model=pic" $(LYNXER_CARGO) rustc -p lynxer_ffi --release \
	    --manifest-path $(LYNXER_RUST_MANIFEST) --target-dir $(LYNXER_RUST_TARGET_DIR) \
	    -- --print native-static-libs > $(LYNXER_FFI_BUILD_LOG) 2>&1 \
	    || { cat $(LYNXER_FFI_BUILD_LOG); exit 1; }
	@cat $(LYNXER_FFI_BUILD_LOG)
	@sed -n 's/.*native-static-libs: //p' $(LYNXER_FFI_BUILD_LOG) | tail -n 1 > $(LYNXER_FFI_NATIVE_LIBS)
	@test -s $(LYNXER_FFI_NATIVE_LIBS) || rm -f $(LYNXER_FFI_NATIVE_LIBS)

$(LYNXER_SIGNATURE_MODULE): $(LYNXER_SIGNATURE_SOURCE) $(LYNXER_DIR)/ffi_abi.h $(LYNXER_DIR)/stdlib/lynxer_native_abi.h
	$(LYNXER_CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $< -o $@

# The Lynxer suite: static module/backend contract check, then the
# interpreter, compiled-executable and bundled-executable parity gates.
# Export-to-C ABI: build a shared library (plus its generated header) from a
# program's `export`s and drive it from a C++ consumer and a Python ctypes
# consumer, then check that invalid exports are rejected with located errors.
LYNXER_EXPORT_FIXTURE := $(LYNXER_DIR)/examples/export_basic.lynx
LYNXER_EXPORT_DIR := $(CLYX_TMP)_exportdir
LYNXER_EXPORT_LIBRARY := $(LYNXER_EXPORT_DIR)/libexport_basic.so
LYNXER_EXPORT_HEADER := $(LYNXER_EXPORT_DIR)/libexport_basic.h
LYNXER_EXPORT_TEST := $(CLYX_TMP)_export_test

testLynxerEmit: lynxerToolchain $(LYNXER_TARGET) $(LYNXER_SHARED) $(LYNXER_NATIVE_BUILT)
	@# Build from a throwaway copy of the source, then delete it and run the
	@# consumers from /tmp. The emitted library embeds the program, so it must
	@# work with neither the .lynx source nor the build directory in reach.
	@mkdir -p $(LYNXER_EXPORT_DIR)
	@cp $(LYNXER_EXPORT_FIXTURE) $(CLYX_TMP)_export_src.lynx
	@$(CLYX) --emit-library $(CLYX_TMP)_export_src.lynx -o $(LYNXER_EXPORT_LIBRARY)
	@rm -f $(CLYX_TMP)_export_src.lynx
	@test -f $(LYNXER_EXPORT_HEADER) || { echo "emit-library did not write $(LYNXER_EXPORT_HEADER)"; exit 1; }
	@grep -q 'echoBytes' $(LYNXER_EXPORT_HEADER) || { echo "generated header is missing an export"; exit 1; }
	@# The checked-in golden header the fixture includes must stay in sync with
	@# what the generator produces.
	@diff -u $(LYNXER_DIR)/examples/libexport_basic.h $(LYNXER_EXPORT_HEADER) > /dev/null || { echo "generated header differs from lynxer/examples/libexport_basic.h"; diff -u $(LYNXER_DIR)/examples/libexport_basic.h $(LYNXER_EXPORT_HEADER); exit 1; }
	@nm -D --defined-only $(LYNXER_EXPORT_LIBRARY) | grep -q ' T add' || { echo "emitted library is missing the 'add' export"; exit 1; }
	@if nm -D --defined-only $(LYNXER_EXPORT_LIBRARY) | grep -q 'lynxer_embed_'; then echo "emitted library leaks embedding symbols"; exit 1; fi
	@$(LYNXER_CXX) -std=c++17 -O2 -I$(LYNXER_DIR) $(LYNXER_DIR)/examples/export_test.cpp $(LYNXER_EXPORT_LIBRARY) -o $(LYNXER_EXPORT_TEST) -Wl,-rpath,$(CURDIR)/$(LYNXER_EXPORT_DIR)
	@cd /tmp && $(CURDIR)/$(LYNXER_EXPORT_TEST)
	@cd /tmp && $(PYTHON) $(CURDIR)/$(LYNXER_DIR)/examples/export_test.py $(CURDIR)/$(LYNXER_EXPORT_LIBRARY)
	@echo "emitted library (with generated header) ran from /tmp with its source removed"
	@# The compiled runtime is mandatory: a missing liblynxer.so must be a clear
	@# failure, not a compiler error.
	@if $(CLYX) --emit-library $(LYNXER_EXPORT_FIXTURE) --runtime /nonexistent/liblynxer.so -o $(CLYX_TMP)_bad.so 2>$(CLYX_TMP)_export_err.log; then echo "expected a missing runtime to fail"; exit 1; fi
	@grep -q "build Lynxer first" $(CLYX_TMP)_export_err.log || { echo "missing-runtime error is unclear"; cat $(CLYX_TMP)_export_err.log; exit 1; }
	@printf '%s\n' 'export "cdecl:value(value)" f(any x) -> any { return x; }' > $(CLYX_TMP)_export_err.lynx
	@if $(CLYX) --emit-library $(CLYX_TMP)_export_err.lynx -o $(CLYX_TMP)_bad.so 2>$(CLYX_TMP)_export_err.log; then echo "expected the 'value' export to be rejected"; exit 1; fi
	@grep -q "do not support the 'value' type" $(CLYX_TMP)_export_err.log || { echo "unexpected error for a 'value' export"; cat $(CLYX_TMP)_export_err.log; exit 1; }
	@printf '%s\n' 'export "cdecl:int64(...)" f(int a) -> int { return a; }' > $(CLYX_TMP)_export_err.lynx
	@if $(CLYX) --emit-library $(CLYX_TMP)_export_err.lynx -o $(CLYX_TMP)_bad.so 2>$(CLYX_TMP)_export_err.log; then echo "expected the packed export to be rejected"; exit 1; fi
	@grep -q "packed signatures" $(CLYX_TMP)_export_err.log || { echo "unexpected error for a packed export"; cat $(CLYX_TMP)_export_err.log; exit 1; }
	@printf '%s\n' 'export "cdecl:int64(int64)" f(int a, int b) -> int { return a + b; }' > $(CLYX_TMP)_export_err.lynx
	@if $(CLYX) --emit-library $(CLYX_TMP)_export_err.lynx -o $(CLYX_TMP)_bad.so 2>$(CLYX_TMP)_export_err.log; then echo "expected the arity mismatch to be rejected"; exit 1; fi
	@grep -q "parameter(s)" $(CLYX_TMP)_export_err.log || { echo "unexpected error for an arity mismatch"; cat $(CLYX_TMP)_export_err.log; exit 1; }
	@printf '%s\n' 'export "cdecl:cstring(cstring)" f(int a) -> str { return ""; }' > $(CLYX_TMP)_export_err.lynx
	@if $(CLYX) --emit-library $(CLYX_TMP)_export_err.lynx -o $(CLYX_TMP)_bad.so 2>$(CLYX_TMP)_export_err.log; then echo "expected the type mismatch to be rejected"; exit 1; fi
	@grep -q "does not match C type" $(CLYX_TMP)_export_err.log || { echo "unexpected error for a type mismatch"; cat $(CLYX_TMP)_export_err.log; exit 1; }
	@printf '%s\n' 'export "cdecl:int64(int64)" f(int a = 1) -> int { return a; }' > $(CLYX_TMP)_export_err.lynx
	@if $(CLYX) --emit-library $(CLYX_TMP)_export_err.lynx -o $(CLYX_TMP)_bad.so 2>$(CLYX_TMP)_export_err.log; then echo "expected the default parameter to be rejected"; exit 1; fi
	@grep -q "default value" $(CLYX_TMP)_export_err.log || { echo "unexpected error for a default parameter"; cat $(CLYX_TMP)_export_err.log; exit 1; }
	@rm -f $(CLYX_TMP)_export_err.lynx $(CLYX_TMP)_export_err.log $(LYNXER_EXPORT_TEST) $(CLYX_TMP)_bad.so
	@rm -rf $(LYNXER_EXPORT_DIR)
	@echo "lynxer export ABI test passed"

testLynxer: lynxerToolchain $(LYNXER_TARGET) $(LYNXER_NATIVE_BUILT) $(LYNXER_SIGNATURE_MODULE) testLynxerInstall $(LYNXER_EMIT_TEST)
	@command -v $(PYTHON) >/dev/null 2>&1 || { echo "lynxer: $(PYTHON) not found; Python 3 is required for $(LYNXER_CONTRACT_CHECK)"; exit 1; }
	@$(PYTHON) $(LYNXER_CONTRACT_CHECK)
	@printf 'Lynxer\n' > $(CLYX_TMP)_stdin
	@output="$$($(CLYX) $(LYNXER_DIR)/examples/hello.lynx < $(CLYX_TMP)_stdin)"; \
	case "$$output" in \
	*"Hello, Lynxer!") ;; \
	*) echo "expected greeting 'Hello, Lynxer!', received: $$output"; exit 1;; \
	esac
	@output="$$($(CLYX) $(LYNXER_CONDITION_FIXTURE))"; \
	expected="$$(printf 'if branch\nelse branch')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected condition output:"; \
	printf '%s\n' "$$expected"; \
	echo "received condition output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(LYNXER_COMMENT_FIXTURE))"; \
	expected="$$(printf '3\n/// this is string content, not a comment')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected comment output:"; \
	printf '%s\n' "$$expected"; \
	echo "received comment output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(LYNXER_LOOP_FIXTURE))"; \
	expected="$$(printf 'while 0\nwhile 2\nfor 0\nfor 1\ndoWhile 1\ndoWhile 3\niterate 0\niterate 1\nforever 2')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected loop output:"; \
	printf '%s\n' "$$expected"; \
	echo "received loop output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(LYNXER_INPUTLN_FIXTURE) < $(CLYX_TMP)_stdin)"; \
	expected="$$(printf 'Hello, world!\nLynxer')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected inputln output:"; printf '%s\n' "$$expected"; \
	echo "received inputln output:"; printf '%s\n' "$$output"; exit 1; fi
	@set +e; \
	output="$$($(CLYX) $(LYNXER_SETUP_ERROR_FIXTURE) 2>&1)"; \
	status=$$?; \
	set -e; \
expected="lynxer: $(LYNXER_SETUP_ERROR_FIXTURE):3:2: program must define global setup()"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected setup enforcement: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@printf 'Alice\nsecret\n42\n2.5\n\ny\nn\n2\n0,2\n1\nline one\nline two\n.\n' > $(CLYX_TMP)_tui_stdin; \
	output="$$($(CLYX) $(LYNXER_TUI_FIXTURE) < $(CLYX_TMP)_tui_stdin 2>&1)"; \
	status=$$?; \
	if [ $$status -ne 0 ]; then \
	echo "tui fixture failed (status $$status)"; \
	printf '%s\n' "$$output"; rm -f $(CLYX_TMP)_tui_stdin; exit 1; fi; \
	expected_output="$$(cat $(LYNXER_TUI_FIXTURE:.lynx=.expected))"; \
	if [ "$$output" != "$$expected_output" ]; then \
	echo "tui fixture output mismatch"; \
	echo "expected:"; printf '%s\n' "$$expected_output"; \
	echo "received:"; printf '%s\n' "$$output"; \
	rm -f $(CLYX_TMP)_tui_stdin; exit 1; fi; \
	rm -f $(CLYX_TMP)_tui_stdin
	@output="$$($(CLYX) $(LYNXER_MILESTONE4_FIXTURE))"; \
	expected="$$(printf "2\\n7\\n5\\n-3\\n-6\\n-8\\n-7\\n12\\n4\\n32\\n-3\\nfalse\\ntrue\\ntrue\\nelif\\ncase\\ncaught: Cannot convert 'not an integer' to int\\n\\033")"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone4 output:"; printf '%s\n' "$$expected"; \
	echo "received milestone4 output:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(LYNXER_MILESTONE4_PATTERN_FIXTURE))"; \
	if [ "$$output" != "2" ]; then \
	echo "expected milestone4 pattern output: 2"; \
	echo "received: $$output"; exit 1; fi
	@set +e; \
	output="$$($(CLYX) $(LYNXER_ERROR_FIXTURE) 2>&1)"; \
	status=$$?; \
	set -e; \
expected="lynxer: $(LYNXER_ERROR_FIXTURE):4:8: unknown variable 'missing'"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected source-located error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@set +e; \
	output="$$($(CLYX) $(LYNXER_MODULE_ERROR_FIXTURE) 2>&1)"; \
	status=$$?; \
	set -e; \
expected="lynxer: $(LYNXER_MODULE_ERROR_LIB):7:23: charAt() index is out of range"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected module-located error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(LYNXER_DIR)/examples/builtins.lynx)"; \
	expected="$$(printf '99 3.14 true\n42 3\n1.5\n[0, 1, 2, 3, 4]\n[2, 3, 4, 5, 6, 7]\n[0, 2, 4, 6, 8]\n[10, 9, 8, 7, 6, 5, 4, 3, 2, 1]\n[]\n[0, 2, 4, 6, 8]\n[5, 3, 9, 1, 7]\n7 [5, 3, 9, 1, 7]\n7\n[100, 3, 9, 1, 7]\n[3, 9]\ntrue false\n2 -1\n5-3-9-1-7\ntrue true false\n25 5\n[1, 3, 5, 7, 9] [9, 7, 5, 3, 1]\n[7, 1, 9, 3, 5]\n1 9\n[5, 3] [1, 7]\n5 7\n1\n[5, 3, 9, 1, 7, 8, 8]\n[5, 50, 3, 9, 1, 7]\n[1, 2, 3, 4]\n[8]\n[x, x, x]\n[{"a": 1, "b": "a"}, {"a": 2, "b": "b"}]\n[1, "two", true, null]\n{"name": "Ada", "age": 36}\n[a, b, c]\ntrue\n(10, 20) (1, two, 3)\n3 two 3\ntrue 0\n(10,)\n[10, 20] (4, 5)\n(10, 20, 1, two, 3)\n1\n1 3\n[10, 20]\n(20, 10)\n(1, 2, 3) (3, 2, 1)\n10 20 30\ntrue true\n(1, 2)\n3\n({"a": 1, "b": "a"}, {"a": 2, "b": "b"})\n10-20\nint float str\nbool none list\n5 5\nMISSING sentinel <sentinel>\n<object> object\n42\n-7\n200\n305419896\n4 8 2\nHello, Ada!\nAda is 36\n40\nno {name} interpolation here\n3')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected builtins output:"; \
	printf '%s\n' "$$expected"; \
	echo "received builtins output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@for fixture in hello conditions loops comments builtins milestone4 milestone5 milestone5_codeblocks; do \
	$(CLYX) $(LYNXER_DIR)/examples/$$fixture.lynx < $(CLYX_TMP)_stdin > $(CLYX_TMP)_direct.out 2>$(CLYX_TMP)_direct.err; \
	direct_status=$$?; \
	if ! $(CLYX) --compile $(LYNXER_DIR)/examples/$$fixture.lynx $(CLYX_TMP)_compiled > /dev/null; then \
	echo "compile failed for $$fixture"; exit 1; fi; \
	$(CLYX_TMP)_compiled < $(CLYX_TMP)_stdin > $(CLYX_TMP)_compiled.out 2>$(CLYX_TMP)_compiled.err; \
	compiled_status=$$?; \
	if ! diff -q $(CLYX_TMP)_direct.out $(CLYX_TMP)_compiled.out > /dev/null || \
	   ! diff -q $(CLYX_TMP)_direct.err $(CLYX_TMP)_compiled.err > /dev/null || \
	   [ "$$direct_status" -ne "$$compiled_status" ]; then \
	echo "compiled-executable parity failed for $$fixture"; \
	diff $(CLYX_TMP)_direct.out $(CLYX_TMP)_compiled.out | head -5; \
	exit 1; \
	fi; \
	done
	@for fixture in $(LYNXER_EXIT_FIXTURES); do \
	run="$(LYNXER_DIR)/examples/$$fixture.lynx"; \
	for mode in direct compiled; do \
	if [ "$$mode" = compiled ]; then \
	if ! $(CLYX) --compile "$$run" $(CLYX_TMP)_exit_test > /dev/null; then \
	echo "compile failed for $$fixture"; exit 1; fi; \
	executable="$(CLYX_TMP)_exit_test"; \
	else executable="$(CLYX)"; fi; \
	set +e; \
	"$$executable" "$$run" > $(CLYX_TMP)_exit.out 2>&1; \
	status=$$?; \
	set -e; \
	expected="$$(printf 'before %s.exit' "$${fixture%_exit}")"; \
	if [ "$$status" -ne 23 ] || [ "$$(cat $(CLYX_TMP)_exit.out)" != "$$expected" ]; then \
	echo "$$mode $$fixture failed (status $$status):"; \
	cat $(CLYX_TMP)_exit.out; rm -f $(CLYX_TMP)_exit.out $(CLYX_TMP)_exit_test; exit 1; fi; \
	done; \
	done; \
	rm -f $(CLYX_TMP)_exit.out $(CLYX_TMP)_exit_test
	@for fixture in $(LYNXER_EXIT_THREAD_FIXTURES); do \
	for mode in direct compiled; do \
	run="$(LYNXER_DIR)/examples/$$fixture.lynx"; \
	if [ "$$mode" = compiled ]; then \
	if ! $(CLYX) --compile "$$run" $(CLYX_TMP)_exit_test > /dev/null; then \
	echo "compile failed for $$fixture"; exit 1; fi; \
	executable="$(CLYX_TMP)_exit_test"; \
	else executable="$(CLYX)"; fi; \
	set +e; \
	"$$executable" "$$run" > $(CLYX_TMP)_exit.out 2>&1; \
	status=$$?; \
	set -e; \
	if [ "$$fixture" = sys_exit_thread ]; then \
	expected="$$(printf 'before sys.exit\nthread cleanup completed')"; exit_status=23; \
	else expected="$$(printf 'before worker\nworker requesting exit')"; exit_status=31; fi; \
	if [ "$$status" -ne "$$exit_status" ] || [ "$$(cat $(CLYX_TMP)_exit.out)" != "$$expected" ]; then \
	echo "$$mode $$fixture failed (status $$status):"; \
	cat $(CLYX_TMP)_exit.out; rm -f $(CLYX_TMP)_exit.out $(CLYX_TMP)_exit_test; exit 1; fi; \
	done; \
	done; \
	rm -f $(CLYX_TMP)_exit.out $(CLYX_TMP)_exit_test
	@for fixture in $(LYNXER_PARITY_FIXTURES); do \
	run="$(LYNXER_DIR)/examples/$$fixture.lynx"; \
	if grep -q '__ARCH__' "$$run"; then \
	sed "s/__ARCH__/$(SYSCALL_ARCH)/g" "$$run" > $(CLYX_TMP)_arch.lynx; \
	run="$(CLYX_TMP)_arch.lynx"; fi; \
	LYNXER_GAME_HEADLESS=1 LYNXER_GRAPHICS_HEADLESS=1 $(CLYX) "$$run" < /dev/null > $(CLYX_TMP)_direct.out 2>&1; \
	direct_status=$$?; \
	if ! $(CLYX) --compile "$$run" $(CLYX_TMP)_compiled > /dev/null; then \
	echo "compile failed for $$fixture (imports)"; exit 1; fi; \
	LYNXER_GAME_HEADLESS=1 LYNXER_GRAPHICS_HEADLESS=1 $(CLYX_TMP)_compiled < /dev/null > $(CLYX_TMP)_compiled.out 2>&1; \
	compiled_status=$$?; \
	if ! diff -q $(CLYX_TMP)_direct.out $(CLYX_TMP)_compiled.out > /dev/null || \
	   [ "$$direct_status" -ne "$$compiled_status" ]; then \
	echo "compiled import parity failed for $$fixture"; \
	diff $(CLYX_TMP)_direct.out $(CLYX_TMP)_compiled.out | head -5; \
	exit 1; \
	fi; \
	rm -f $(CLYX_TMP)_arch.lynx; \
	done
	@$(CLYX) --bundle $(LYNXER_DIR)/examples/hello.lynx $(CLYX_TMP)_bundled > /dev/null; \
	bundled_output="$$($(CLYX_TMP)_bundled < $(CLYX_TMP)_stdin)"; \
	case "$$bundled_output" in \
	*"Hello, Lynxer!") ;; \
	*) echo "bundled executable output mismatch: $$bundled_output"; exit 1;; \
	esac
	@$(CLYX) --compile $(LYNXER_DIR)/examples/bundle_app.lynx $(LYNXER_DIR)/examples/bundle_extras/greeter.lynx $(LYNXER_DIR)/examples/bundle_extras/counter.lynx -o $(CLYX_TMP)_multi > /dev/null; \
	multi_output="$$($(CLYX_TMP)_multi)"; \
	expected="$$(printf 'hello, LYNXER!\n1,2,3')"; \
	if [ "$$multi_output" != "$$expected" ]; then \
	echo "multi-file bundle output mismatch:"; printf '%s\n' "$$multi_output"; exit 1; fi
	@$(CLYX) --compile $(LYNXER_DIR)/examples/bundle_assets.lynx --include $(LYNXER_DIR)/examples/bundle_extras/message.txt -o $(CLYX_TMP)_assets > /dev/null; \
	assets_output="$$($(CLYX_TMP)_assets)"; \
	expected="$$(printf '[message.txt]\nbundled payload')"; \
	if [ "$$assets_output" != "$$expected" ]; then \
	echo "included data file output mismatch:"; printf '%s\n' "$$assets_output"; exit 1; fi
	@printf 'CLYXC\x00\x01\x01not-real-bytecode' > $(CLYX_TMP)_bad.lynxc; \
	if $(CLYX) $(CLYX_TMP)_bad.lynxc > /dev/null 2>$(CLYX_TMP)_bad.err; then \
	echo "legacy bytecode file was accepted"; exit 1; \
	fi; \
	grep -q "no longer supported" $(CLYX_TMP)_bad.err || { echo "bytecode removal error missing"; exit 1; }; \
	rm -f $(CLYX_TMP)_bad.lynxc $(CLYX_TMP)_bad.err $(CLYX_TMP)_direct.out $(CLYX_TMP)_direct.err \
	$(CLYX_TMP)_compiled.out $(CLYX_TMP)_compiled.err $(CLYX_TMP)_compiled $(CLYX_TMP)_bundled \
	$(CLYX_TMP)_multi $(CLYX_TMP)_assets
	@output="$$($(CLYX) $(LYNXER_DIR)/examples/milestone3.lynx)"; \
	expected="$$(printf '9\nAda\n90\n5\nEngineer\n6\n42\n1\n0\n255\n-12\n0.25\nA\n[1, 2, 3]\n(left, 2)')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone3 output:"; \
	printf '%s\n' "$$expected"; \
	echo "received milestone3 output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(LYNXER_DIR)/examples/m3_scalars.lynx)"; \
	expected="$$(printf '3.5\nZ\n1\n0\n255\n-128\n-300\n100000\n5000000000\n200\n60000\n3000000000\n9000000000\n1.5\n2.25\n[1, 2, 3]\n(10, 20)\n42\n9')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected m3_scalars output:"; \
	printf '%s\n' "$$expected"; \
	echo "received m3_scalars output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(LYNXER_DIR)/examples/m3_scope.lynx)"; \
	expected="$$(printf '1')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected m3_scope output (flat per-function scope):"; \
	printf '%s\n' "$$expected"; \
	echo "received m3_scope output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@set +e; \
	output="$$($(CLYX) $(LYNXER_DIR)/examples/m3_errors.lynx 2>&1)"; \
	status=$$?; \
	set -e; \
	expected="lynxer: $(LYNXER_DIR)/examples/m3_errors.lynx:4:5: value 200 is out of range for type 'int8'"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected out-of-range error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@set +e; \
	output="$$($(CLYX) $(LYNXER_DIR)/examples/m3_const.lynx 2>&1)"; \
	status=$$?; \
	set -e; \
	expected="lynxer: $(LYNXER_DIR)/examples/m3_const.lynx:5:5: variable 'frozen' is constant and cannot be reassigned"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected const reassignment error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@set +e; \
	output="$$($(CLYX) $(LYNXER_DIR)/examples/m3_vargroup.lynx 2>&1)"; \
	status=$$?; \
	set -e; \
	expected="lynxer: $(LYNXER_DIR)/examples/m3_vargroup.lynx:8:5: Vargroup and legacy class-field assignment requires an explicit type"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected vargroup explicit-type error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(LYNXER_MILESTONE5_FIXTURE))"; \
	expected="$$(printf '5\n7\n25')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone5 output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(LYNXER_MILESTONE5_CODEBLOCK_FIXTURE))"; \
	expected="$$(printf 'one\ntwo\nsaved\nafter')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone5 codeblock output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(LYNXER_PATH_IMPORT_FIXTURE))"; \
	expected="$$(cat $(LYNXER_PATH_IMPORT_FIXTURE:.lynx=.expected))"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected path-import output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(LYNXER_MACRO_FIXTURE))"; \
	expected="$$(cat $(LYNXER_MACRO_FIXTURE:.lynx=.expected))"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected macro output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@set +e; output="$$($(CLYX) --lint $(LYNXER_DIR)/examples/macros_private_import.lynx 2>&1)"; status=$$?; set -e; \
	if [ "$$status" -ne 1 ] || ! printf '%s' "$$output" | grep -q "unknown macro 'privatePrint'"; then \
	echo "private macro unexpectedly visible to importer:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(LYNXER_MILESTONE6_FIXTURE))"; \
	expected="$$(printf '6\n42\n6\n6\n7\n\033[31mok\033[0m\ntrue\ncba\n4')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone6 output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(LYNXER_MILESTONE6_MATH_FIXTURE))"; \
	expected="$$(printf '3\n0\ntrue\ntrue\ntrue is now a string.\n42 is a integer.\nint is the type of number.')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native math output:"; printf '%s\n' "$$expected"; \
	echo "received native math output:"; printf '%s\n' "$$output"; exit 1; fi
	@if [ -n "$(LYNXER_NATIVE_STDLIB_FIXTURE)" ]; then \
	output="$$($(CLYX) $(LYNXER_NATIVE_STDLIB_FIXTURE))"; \
	expected="$$(printf '3.141592653589793\n180\ntrue\ntrue\ntrue\nlinux')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native stdlib output:"; printf '%s\n' "$$expected"; \
	echo "received native stdlib output:"; printf '%s\n' "$$output"; exit 1; fi; \
	fi
	@output="$$(LYNXER_GAME_HEADLESS=1 LYNXER_GRAPHICS_HEADLESS=1 $(CLYX) $(LYNXER_PROGRAM_ARGS_FIXTURE) alpha "beta gamma" 2>&1)"; \
	expected="$$(cat $(LYNXER_PROGRAM_ARGS_FIXTURE:.lynx=.expected))"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected program-args output:"; printf '%s\n' "$$expected"; \
	echo "received program-args output:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(LYNXER_SIGNATURE_FIXTURE))"; \
	expected="$$(printf '7\n2.5\nzero\n7\n9\ncopy\n4\n1\n---\n6\n1.25\n4\n6.5\n4\n3.5\n1.500000\n2.75\nabcd\n3.75\nabc\nab5\nabc5\n7\nn12\n3\n9\n10\n10\n0\n5\nABC')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native signature output:"; printf '%s\n' "$$expected"; \
	echo "received native signature output:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(LYNXER_DIR)/examples/native_aggregate.lynx)"; \
	expected="$$(cat $(LYNXER_DIR)/examples/native_aggregate.expected)"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "native typed aggregate round-trip mismatch:"; \
	echo "expected: $$expected"; echo "received: $$output"; exit 1; fi
	@list_output="$$(cd /tmp && "$(CURDIR)/$(LYNXER_TARGET)" --list-stdlibs)"; \
	for module in $(LYNXER_LIST_STDLIB_MODULES); do \
	if ! printf '%s\n' "$$list_output" | grep -Fqx "  $$module"; then \
	echo "stdlib listing is missing module: $$module"; exit 1; fi; \
	done
	@if [ -z "$(HAVE_AUDIO)" ]; then \
	echo "lynxer: skipping $(notdir $(LYNXER_SOUND_FIXTURE)): no audio device (/dev/snd/controlC*) on this host"; fi
	@for fixture in $(LYNXER_STDLIB_FIXTURES) $(LYNXER_AUDIO_FIXTURES) $(LYNXER_DEPRECATED_FIXTURES); do \
	expected="$${fixture%.lynx}.expected"; \
	if [ ! -f "$$expected" ]; then \
	echo "missing expected output for $$fixture"; exit 1; fi; \
	LYNXER_GAME_HEADLESS=1 LYNXER_GRAPHICS_HEADLESS=1 $(CLYX) "$$fixture" > $(CLYX_TMP)_stdlib.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "stdlib fixture failed: $$fixture"; cat $(CLYX_TMP)_stdlib.out; \
	rm -f $(CLYX_TMP)_stdlib.out; exit 1; fi; \
	if ! diff -u "$$expected" $(CLYX_TMP)_stdlib.out > $(CLYX_TMP)_stdlib.diff; then \
	echo "stdlib fixture output mismatch: $$fixture"; \
	cat $(CLYX_TMP)_stdlib.diff; \
	rm -f $(CLYX_TMP)_stdlib.out $(CLYX_TMP)_stdlib.diff; exit 1; fi; \
	done; \
	rm -f $(CLYX_TMP)_stdlib.out $(CLYX_TMP)_stdlib.diff
	@if [ -z "$(HAVE_TLS_TOOLS)" ]; then \
	echo "lynxer: skipping $(notdir $(LYNXER_SERVER_TLS_FIXTURE)): needs curl and openssl on PATH"; \
	else \
	LYNXER_GAME_HEADLESS=1 LYNXER_GRAPHICS_HEADLESS=1 $(CLYX) $(LYNXER_SERVER_TLS_FIXTURE) > $(CLYX_TMP)_tls.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "stdlib fixture failed: $(LYNXER_SERVER_TLS_FIXTURE)"; cat $(CLYX_TMP)_tls.out; \
	rm -f $(CLYX_TMP)_tls.out; exit 1; fi; \
	if ! diff -u $(LYNXER_SERVER_TLS_FIXTURE:.lynx=.expected) $(CLYX_TMP)_tls.out; then \
	echo "stdlib fixture output mismatch: $(LYNXER_SERVER_TLS_FIXTURE)"; \
	rm -f $(CLYX_TMP)_tls.out; exit 1; fi; \
	rm -f $(CLYX_TMP)_tls.out; fi
	@for fixture in $(LYNXER_MILESTONE7_NEW_FIXTURES) $(LYNXER_OWNERSHIP_FIXTURES) $(LYNXER_TUPLE_FIXTURES) $(LYNXER_RANGE_FIXTURES) $(LYNXER_LOWLEVEL_FIXTURES); do \
	expected="$${fixture%.lynx}.expected"; \
	if [ ! -f "$$expected" ]; then \
	echo "missing expected output for $$fixture"; exit 1; fi; \
	run="$$fixture"; \
	if grep -q '__ARCH__' "$$fixture"; then \
	run="$(CLYX_TMP)_arch.lynx"; \
	sed "s/__ARCH__/$(SYSCALL_ARCH)/g" "$$fixture" > "$$run"; fi; \
	output="$$($(CLYX) "$$run" 2>&1)"; \
	status=$$?; \
	rm -f $(CLYX_TMP)_arch.lynx; \
	expected_output="$$(cat "$$expected")"; \
	if [ $$status -ne 0 ]; then \
	echo "builtin fixture failed: $$fixture"; printf '%s\n' "$$output"; \
	exit 1; fi; \
	if [ "$$output" != "$$expected_output" ]; then \
	echo "builtin fixture output mismatch: $$fixture"; \
	echo "expected:"; printf '%s\n' "$$expected_output"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi; \
	done
	@$(CLYX) $(LYNXER_OPTIMIZER_FIXTURE) > $(CLYX_TMP)_opt.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "optimizer fixture failed: $(LYNXER_OPTIMIZER_FIXTURE)"; \
	cat $(CLYX_TMP)_opt.out; rm -f $(CLYX_TMP)_opt.out; exit 1; fi; \
	if ! diff -u $(LYNXER_OPTIMIZER_EXPECTED) $(CLYX_TMP)_opt.out; then \
	echo "optimizer fixture output mismatch"; rm -f $(CLYX_TMP)_opt.out; exit 1; fi
	@$(CLYX) --no-opt $(LYNXER_OPTIMIZER_FIXTURE) > $(CLYX_TMP)_noopt.out 2>&1; \
	if ! diff -u $(CLYX_TMP)_opt.out $(CLYX_TMP)_noopt.out; then \
	echo "optimized and --no-opt runs of the optimizer fixture differ"; \
	rm -f $(CLYX_TMP)_opt.out $(CLYX_TMP)_noopt.out; exit 1; fi; \
	rm -f $(CLYX_TMP)_opt.out $(CLYX_TMP)_noopt.out
	@LYNXER_OPT_REPORT=1 $(CLYX) $(LYNXER_OPTIMIZER_FIXTURE) > /dev/null 2> $(CLYX_TMP)_opt.report; \
	if ! grep -Eq 'constant folds=[1-9][0-9]*' $(CLYX_TMP)_opt.report || \
	   ! grep -Eq 'short-circuits=[1-9][0-9]*' $(CLYX_TMP)_opt.report || \
	   ! grep -Eq 'dead branches=[1-9][0-9]*' $(CLYX_TMP)_opt.report; then \
	echo "optimizer did not exercise every transformation class:"; \
	cat $(CLYX_TMP)_opt.report; rm -f $(CLYX_TMP)_opt.report; exit 1; fi; \
	rm -f $(CLYX_TMP)_opt.report
	@$(CLYX) $(LYNXER_OPTIMIZER_DEPRECATED_FIXTURE) > $(CLYX_TMP)_dep.out 2> $(CLYX_TMP)_dep.err; \
	if [ "$$(cat $(CLYX_TMP)_dep.out)" != "1" ]; then \
	echo "deprecated operator result changed under optimization"; \
	rm -f $(CLYX_TMP)_dep.out $(CLYX_TMP)_dep.err; exit 1; fi; \
	if ! grep -q "operator '&' is deprecated" $(CLYX_TMP)_dep.err; then \
	echo "optimizer swallowed the deprecation warning"; \
	rm -f $(CLYX_TMP)_dep.out $(CLYX_TMP)_dep.err; exit 1; fi; \
	rm -f $(CLYX_TMP)_dep.out $(CLYX_TMP)_dep.err
	@cp $(LYNXER_FORMATTER_INPUT) $(CLYX_TMP)_format.lynx; \
	$(CLYX) --format $(CLYX_TMP)_format.lynx > /dev/null || \
	{ echo "lynxer --format failed on the formatter fixture"; \
	rm -f $(CLYX_TMP)_format.lynx; exit 1; }; \
	if ! diff -u $(LYNXER_FORMATTER_EXPECTED) $(CLYX_TMP)_format.lynx; then \
	echo "formatter output mismatch"; rm -f $(CLYX_TMP)_format.lynx; exit 1; fi; \
	cp $(CLYX_TMP)_format.lynx $(CLYX_TMP)_format2.lynx; \
	$(CLYX) --format $(CLYX_TMP)_format2.lynx > /dev/null || \
	{ echo "second --format pass failed"; exit 1; }; \
	if ! diff -u $(CLYX_TMP)_format.lynx $(CLYX_TMP)_format2.lynx; then \
	echo "formatter is not idempotent"; \
	rm -f $(CLYX_TMP)_format.lynx $(CLYX_TMP)_format2.lynx; exit 1; fi; \
	$(CLYX) $(CLYX_TMP)_format.lynx > /dev/null || \
	{ echo "formatted file does not run"; exit 1; }; \
	cp $(LYNXER_FORMATTER_INPUT) $(CLYX_TMP)_oneline.lynx; \
	$(CLYX) --format-oneline $(CLYX_TMP)_oneline.lynx > /dev/null || \
	{ echo "lynxer --format-oneline failed"; exit 1; }; \
	if [ "$$(wc -l < $(CLYX_TMP)_oneline.lynx)" -ne 0 ]; then \
	echo "one-line formatter left newlines in the file"; \
	rm -f $(CLYX_TMP)_oneline.lynx; exit 1; fi; \
	$(CLYX) $(CLYX_TMP)_oneline.lynx > /dev/null || \
	{ echo "one-line formatted file does not run"; exit 1; }; \
	rm -f $(CLYX_TMP)_format.lynx $(CLYX_TMP)_format2.lynx $(CLYX_TMP)_oneline.lynx
	@$(CLYX) --validate-executeable > /dev/null || \
	{ echo "lynxer --validate-executeable reported a failure:"; \
	$(CLYX) --validate-executeable; exit 1; }
	@if [ -n "$(LYNXER_STDLIB_TEST_ALL)" ]; then \
	LYNXER_GAME_HEADLESS=1 LYNXER_GRAPHICS_HEADLESS=1 LYNXER_SKIP_DISPLAY=$(LYNXER_SKIP_DISPLAY) $(CLYX) $(LYNXER_STDLIB_TEST_ALL) > $(CLYX_TMP)_stdlib_all.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "consolidated stdlib test failed: $(LYNXER_STDLIB_TEST_ALL)"; \
	cat $(CLYX_TMP)_stdlib_all.out; \
	rm -f $(CLYX_TMP)_stdlib_all.out; exit 1; fi; \
	rm -f $(CLYX_TMP)_stdlib_all.out; \
	fi
	@if [ "$(LYNXER_SKIP_DISPLAY)" = "1" ]; then \
	echo "lynxer: skipping game_clicker.lynx: display tests disabled (LYNXER_SKIP_DISPLAY=1)"; \
	else \
	LYNXER_GAME_HEADLESS=1 LYNXER_GRAPHICS_HEADLESS=1 $(CLYX) $(LYNXER_DIR)/examples/game_clicker.lynx >/dev/null 2>&1 || \
	{ echo "example failed: $(LYNXER_DIR)/examples/game_clicker.lynx"; exit 1; }; \
	fi
	@rm -f $(CLYX_TMP)_stdin $(CLYX_TMP)_tui_stdin
	@echo "lynxer smoke test passed"

# Run bounded, real-window graphics and game smoke tests. The caller must
# provide an X11 display with an OpenGL implementation (CI uses Xvfb + Mesa).
testLynxerGui: lynxerToolchain $(LYNXER_TARGET) $(LYNXER_NATIVE_BUILT)
	@test -n "$$DISPLAY" || { echo "lynxer: testLynxerGui requires an X11 display"; exit 1; }
	@for fixture in $(LYNXER_GUI_FIXTURES); do \
	expected="$${fixture%.lynx}.expected"; \
	output="$(CLYX_TMP)_gui.out"; \
	if ! timeout 30s env LYNXER_GRAPHICS_HEADLESS=0 LYNXER_GAME_HEADLESS=0 $(CLYX) "$$fixture" > "$$output" 2>&1; then \
	echo "windowed GUI fixture failed: $$fixture"; cat "$$output"; rm -f "$$output"; exit 1; fi; \
	if ! diff -u "$$expected" "$$output"; then \
	echo "windowed GUI fixture output mismatch: $$fixture"; rm -f "$$output"; exit 1; fi; \
	rm -f "$$output"; \
	done; \
	rm -f "$(CLYX_TMP)_gui.out"
	@echo "lynxer windowed GUI smoke tests passed"

# `lynxer --install` must produce a self-contained tree that resolves its
# stdlib from any working directory. The default prefix is /usr and needs root,
# so this gate installs into a throwaway prefix and drives the installed symlink
# from /tmp, where none of the repository's own stdlib paths exist.
testLynxerInstall: lynxerToolchain $(LYNXER_TARGET) $(LYNXER_NATIVE_BUILT)
	@rm -rf $(LYNXER_INSTALL_PREFIX)
	@printf 'global setup(){ import("math"); }\nglobal main(){ println(global.math.sqrt(9)); }\n' > $(CLYX_TMP)_install.lynx
	@LYNXER_PREFIX="$(CURDIR)/$(LYNXER_INSTALL_PREFIX)" $(CLYX) --install > /dev/null
	@if [ ! -x "$(CURDIR)/$(LYNXER_INSTALLED_BIN)" ]; then \
	echo "install did not create $(LYNXER_INSTALLED_BIN)"; exit 1; fi
	@output="$$(cd /tmp && "$(CURDIR)/$(LYNXER_INSTALLED_BIN)" "$(CURDIR)/$(CLYX_TMP)_install.lynx")"; \
	if [ "$$output" != "3" ]; then \
	echo "installed interpreter did not resolve imports from /tmp: $$output"; \
	rm -f $(CLYX_TMP)_install.lynx; exit 1; fi
	@listing="$$(cd /tmp && "$(CURDIR)/$(LYNXER_INSTALLED_BIN)" --list-stdlibs)"; \
	if ! printf '%s\n' "$$listing" | grep -Fqx "  math"; then \
	echo "installed --list-stdlibs did not find the stdlib"; \
	rm -f $(CLYX_TMP)_install.lynx; exit 1; fi
	@LYNXER_PREFIX="$(CURDIR)/$(LYNXER_INSTALL_PREFIX)" $(CLYX) --uninstall > /dev/null
	@if [ -e "$(CURDIR)/$(LYNXER_INSTALLED_BIN)" ] || [ -e "$(CURDIR)/$(LYNXER_INSTALL_PREFIX)/lib/lynxer" ]; then \
	echo "uninstall left files under $(LYNXER_INSTALL_PREFIX)"; exit 1; fi
	@rm -rf $(LYNXER_INSTALL_PREFIX)
	@rm -f $(CLYX_TMP)_install.lynx
	@echo "lynxer install test passed"

# Architecture-specific syscall gates. `uname -m` is asserted so the AMD64 and
# ARM64 CI jobs each run the matching fixture and a mistake fails loudly rather
# than testing the wrong architecture.
testLynxerAmd64Syscalls: lynxerToolchain $(LYNXER_TARGET)
	@test "$$(uname -m)" = "x86_64" || \
	{ echo "lynxer: $(notdir $(LYNXER_AMD64_SYSCALL_FIXTURE)) requires an x86_64 host (uname -m = $$(uname -m))"; exit 1; }
	@$(CLYX) $(LYNXER_AMD64_SYSCALL_FIXTURE) > $(CLYX_TMP)_amd64_syscalls.out 2>&1; \
	status=$$?; \
	if [ $$status -ne 0 ]; then \
	echo "amd64 syscall fixture failed (status $$status)"; \
	cat $(CLYX_TMP)_amd64_syscalls.out; \
	rm -f $(CLYX_TMP)_amd64_syscalls.out; exit 1; fi; \
	if ! diff -u $(LYNXER_AMD64_SYSCALL_FIXTURE:.lynx=.expected) $(CLYX_TMP)_amd64_syscalls.out; then \
	echo "amd64 syscall fixture output mismatch"; \
	rm -f $(CLYX_TMP)_amd64_syscalls.out; exit 1; fi; \
	rm -f $(CLYX_TMP)_amd64_syscalls.out
	@echo "lynxer amd64 syscall fixture passed"

testLynxerArm64Syscalls: lynxerToolchain $(LYNXER_TARGET)
	@test "$$(uname -m)" = "aarch64" || \
	{ echo "lynxer: $(notdir $(LYNXER_ARM64_SYSCALL_FIXTURE)) requires an aarch64 host (uname -m = $$(uname -m))"; exit 1; }
	@$(CLYX) $(LYNXER_ARM64_SYSCALL_FIXTURE) > $(CLYX_TMP)_arm64_syscalls.out 2>&1; \
	status=$$?; \
	if [ $$status -ne 0 ]; then \
	echo "arm64 syscall fixture failed (status $$status)"; \
	cat $(CLYX_TMP)_arm64_syscalls.out; \
	rm -f $(CLYX_TMP)_arm64_syscalls.out; exit 1; fi; \
	if ! diff -u $(LYNXER_ARM64_SYSCALL_FIXTURE:.lynx=.expected) $(CLYX_TMP)_arm64_syscalls.out; then \
	echo "arm64 syscall fixture output mismatch"; \
	rm -f $(CLYX_TMP)_arm64_syscalls.out; exit 1; fi; \
	rm -f $(CLYX_TMP)_arm64_syscalls.out
	@echo "lynxer arm64 syscall fixture passed"

clean:
	@find . -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null || true
	@find . -name '*.pyc' -delete 2>/dev/null || true
	@rm -f $(CLYX_TMP)_* 2>/dev/null || true
	@echo "✓ Cleaned transient files."

cleanLynxer:
	@rm -f $(LYNXER_TARGET) $(LYNXER_TARGET)-arm64 $(LYNXER_OBJECTS) $(LYNXER_OBJECTS_ARM64)
	@rm -f $(LYNXER_FFI_BUILD_LOG) $(LYNXER_FFI_NATIVE_LIBS)
	@rm -f $(LYNXER_NATIVE_MODULES) $(LYNXER_SIGNATURE_MODULE) $(LYNXER_SHARED) $(CLYX_TMP)_*
	@rm -rf $(LYNXER_INSTALL_PREFIX)
	@rm -f $(LYNXER_DIR)/stdlib/ffi.so
	@rm -rf $(LYNXER_DIR)/build $(LYNXER_RUST_DIR)/target $(LYNXER_RUST_DIR)/*/target
	@echo "✓ Cleaned Lynxer build artifacts."

cleanAll: clean cleanLynxer cleanBob
	@echo "✓ Cleaned all generated build artifacts."

help:
	@echo "Lynxer build targets:"
	@echo "  make build              (interpreter + every stdlib module, C++ and Rust)"
	@echo "  make buildAll           (alias for build)"
	@echo "  make buildLynxer"
	@echo "  make buildLynxerArm64"
	@echo "  make buildBob           (Bob, the package manager in Bob/)"
	@echo "  make cargo"
	@echo "  make test               (Lynxer suite)"
	@echo "  make testLynxer        (Lynxer suite only)"
	@echo "  make testLynxerInstall (--install into a throwaway prefix, no root)"
	@echo "  make testLynxerAmd64Syscalls   (amd64 syscall fixture; x86_64 host)"
	@echo "  make testLynxerArm64Syscalls   (arm64 syscall fixture; aarch64 host)"
	@echo "  make check"
	@echo "  make clean"
	@echo "  make cleanLynxer"
	@echo "  make cleanBob"
	@echo "  make cleanAll"
	@echo "  make help"
	@echo ""
	@echo "Lynxer source commands:"
	@echo "  ./lynxer/lynxer --format <file.lynx>"
	@echo "  ./lynxer/lynxer --format-oneline <file.lynx>"
	@echo "  ./lynxer/lynxer --ast <file.lynx>"
	@echo "  ./lynxer/lynxer --lint <file.lynx>"
