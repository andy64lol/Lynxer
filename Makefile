PYTHON   ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)

# --- Lynxer: the standalone C++ implementation -----------------------------
# Every path is repo-root relative.
LYNXER_DIR := lynxer
LYNXER_TARGET := $(LYNXER_DIR)/lynxer
LYNXER_SOURCES := $(addprefix $(LYNXER_DIR)/,main.cpp shell.cpp lexer.cpp runtime.cpp types.cpp builtins.cpp ops.cpp ast.cpp optimizer.cpp formatter.cpp parser.cpp config.cpp bundle.cpp interrupt.cpp)
LYNXER_OBJECTS := $(LYNXER_SOURCES:.cpp=.o)
LYNXER_OBJECTS_ARM64 := $(LYNXER_SOURCES:.cpp=.o-arm64)
LYNXER_HEADERS := $(wildcard $(LYNXER_DIR)/*.hpp)
LYNXER_CXX ?= c++
LYNXER_CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -fPIE

# Native (C++) stdlib modules: every lynxer/stdlib/<name>.cpp -> <name>.so.
LYNXER_NATIVE_SOURCES := $(wildcard $(LYNXER_DIR)/stdlib/*.cpp)
LYNXER_NATIVE_MODULES := $(LYNXER_NATIVE_SOURCES:.cpp=.so)

# Rust backends: self-contained cdylibs that the interpreter dlopens directly.
LYNXER_RUST_DIR := $(LYNXER_DIR)/rust
LYNXER_RUST_TARGET_DIR := $(LYNXER_DIR)/build/rust
LYNXER_RUST_MANIFEST := $(LYNXER_RUST_DIR)/Cargo.toml
LYNXER_RUST_SOURCES := $(wildcard $(LYNXER_RUST_DIR)/*/src/*.rs) \
                        $(wildcard $(LYNXER_RUST_DIR)/*/Cargo.toml) \
                        $(LYNXER_RUST_MANIFEST) $(LYNXER_RUST_DIR)/Cargo.lock
LYNXER_RUST_MODULE_NAMES := game image json lua network server sound sqldb tui ffi
LYNXER_RUST_MODULES := $(LYNXER_RUST_MODULE_NAMES:%=$(LYNXER_DIR)/stdlib/%.so)

# Rust-backed modules are gated on the Rust toolchain.
HAVE_CARGO := $(shell command -v cargo >/dev/null 2>&1 && echo 1)
ifeq ($(HAVE_CARGO),1)
LYNXER_RUST_BUILT := $(LYNXER_RUST_MODULES)
else
LYNXER_RUST_BUILT :=
endif

LYNXER_CORE_MODULES := $(filter-out $(LYNXER_RUST_MODULES),$(LYNXER_NATIVE_MODULES))
LYNXER_NATIVE_BUILT := $(LYNXER_CORE_MODULES) $(LYNXER_RUST_BUILT)

# Lynxer suite fixtures and static contract check.
LYNXER_CONTRACT_CHECK := $(LYNXER_DIR)/scripts/check_module_contracts.py
LYNXER_GOLDEN_CHECK := $(LYNXER_DIR)/scripts/check_golden.py
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
LYNXER_NATIVE_STDLIB_FIXTURE := $(LYNXER_DIR)/examples/native_stdlibs.lynx
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
# Display/audio tests. CI runners have neither a display nor an audio device, and
# the graphics backend crashes without a display, so the workflows pass
# LYNXER_SKIP_DISPLAY=1 to drop the fixtures that need one.
LYNXER_SKIP_DISPLAY ?= 0
ifeq ($(LYNXER_SKIP_DISPLAY),1)
LYNXER_DISPLAY_FIXTURES := stdlib_game stdlib_sound
else
LYNXER_DISPLAY_FIXTURES :=
endif
LYNXER_DISPLAY_FIXTURE_FILES := $(LYNXER_DISPLAY_FIXTURES:%=$(LYNXER_DIR)/examples/%.lynx)
LYNXER_STDLIB_FIXTURES := $(filter-out $(LYNXER_SOUND_FIXTURE) $(LYNXER_DISPLAY_FIXTURE_FILES),$(wildcard $(LYNXER_DIR)/examples/stdlib_*.lynx))
ifeq ($(HAVE_AUDIO),1)
LYNXER_AUDIO_FIXTURES := $(filter-out $(LYNXER_DISPLAY_FIXTURE_FILES),$(LYNXER_SOUND_FIXTURE))
else
LYNXER_AUDIO_FIXTURES :=
endif
# Built-in-family fixtures (lynxer/examples/builtin_<name>.lynx) do too.
LYNXER_MILESTONE7_NEW_FIXTURES := $(LYNXER_DIR)/examples/builtin_async.lynx $(LYNXER_DIR)/examples/builtin_ffi.lynx
# Ownership/borrowing built-ins: moves, borrows, swaps and their error paths.
LYNXER_OWNERSHIP_FIXTURES := $(LYNXER_DIR)/examples/ownership.lynx
# Low-level fixtures: native-memory typed/endian access and the portable named
# syscalls. They assert only host-independent behaviour, so the same expected
# output holds on amd64 and arm64, and both CI jobs run them.
LYNXER_LOWLEVEL_FIXTURES := $(LYNXER_DIR)/examples/lowlevel_memory.lynx \
	$(LYNXER_DIR)/examples/lowlevel_syscalls.lynx \
	$(LYNXER_DIR)/examples/lowlevel_arch.lynx \
	$(LYNXER_DIR)/examples/native_memory_structs.lynx \
	$(LYNXER_DIR)/examples/memory_atomics.lynx \
	$(LYNXER_DIR)/examples/raw_addresses.lynx \
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
LYNXER_LIST_STDLIB_MODULES := cli colorlib csv debug fileIO game image js json lua math \
	multiprocessing network os path random re regex server shell sound sqldb sys text time tui typing
# Import-parity fixtures (interpreted vs compiled). The sound one needs a device.
LYNXER_PARITY_FIXTURES := native_stdlibs milestone6_module milestone6_math_native stdlib_json \
	stdlib_re stdlib_path stdlib_game stdlib_image stdlib_lua stdlib_sqldb stdlib_tui deprecated_operators optimizer \
	lowlevel_memory lowlevel_syscalls lowlevel_arch language_fields ownership
ifeq ($(HAVE_AUDIO),1)
LYNXER_PARITY_FIXTURES += stdlib_sound
endif
LYNXER_PARITY_FIXTURES := $(filter-out $(LYNXER_DISPLAY_FIXTURES),$(LYNXER_PARITY_FIXTURES))

# Recipe shorthands: the interpreter, and the temp-file prefix for the suite.
CLYX := ./$(LYNXER_TARGET)
CLYX_TMP := $(LYNXER_DIR)/.lynxer

.PHONY: all cargo build buildAll buildLynxer buildLynxerArm64 test testLynxer testLynxerAmd64Syscalls testLynxerArm64Syscalls check clean cleanLynxer cleanAll help

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

# Binary plus every stdlib module (C++, and the Rust ones when cargo exists).
buildLynxer: $(LYNXER_TARGET) $(LYNXER_NATIVE_BUILT)
	@echo "✓ Lynxer build complete: $(LYNXER_TARGET)"

# ARM64 (aarch64) binary. Requires aarch64-linux-gnu-g++ installed.
buildLynxerArm64: $(LYNXER_TARGET)-arm64 $(LYNXER_NATIVE_BUILT)
	@echo "✓ Lynxer ARM64 build complete: $(LYNXER_TARGET)-arm64"

# Rust backends only. Skipped with a message when cargo is not installed.
ifeq ($(HAVE_CARGO),1)
cargo: $(LYNXER_RUST_MODULES)
	@echo "✓ Lynxer Rust backends ready ($(LYNXER_RUST_TARGET_DIR))"
else
cargo:
	@echo "lynxer: skipping the Rust backends: cargo not found in PATH"
endif

$(LYNXER_TARGET): $(LYNXER_OBJECTS)
	$(LYNXER_CXX) $(LYNXER_CXXFLAGS) $(LYNXER_OBJECTS) -o $@

$(LYNXER_TARGET)-arm64: $(LYNXER_OBJECTS_ARM64)
	@command -v aarch64-linux-gnu-g++ >/dev/null || { echo "error: aarch64-linux-gnu-g++ not found"; exit 1; }
	@aarch64-linux-gnu-g++ $(LYNXER_CXXFLAGS) $(filter %.o-arm64,$^) -o $@ -static-libstdc++ -static-libgcc

$(LYNXER_DIR)/%.o: $(LYNXER_DIR)/%.cpp $(LYNXER_HEADERS)
	$(LYNXER_CXX) $(LYNXER_CXXFLAGS) -c $< -o $@

$(LYNXER_DIR)/%.o-arm64: $(LYNXER_DIR)/%.cpp $(LYNXER_HEADERS)
	@command -v aarch64-linux-gnu-g++ >/dev/null || { echo "error: aarch64-linux-gnu-g++ not found"; exit 1; }
	@aarch64-linux-gnu-g++ $(LYNXER_CXXFLAGS) -c $< -o $@

# Remaining C++ stdlib modules, compiled to lynxer/stdlib/<name>.so.
$(LYNXER_DIR)/stdlib/%.so: $(LYNXER_DIR)/stdlib/%.cpp
	$(LYNXER_CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $< -o $@

# Rust backends: self-contained cdylibs exporting lynxer_module_init_v1 and
# their ops, copied to lynxer/stdlib/<name>.so for the interpreter to dlopen.
$(LYNXER_RUST_TARGET_DIR)/release/liblynxer_%.so: $(LYNXER_RUST_SOURCES)
	@command -v cargo >/dev/null || { echo "lynxer: cargo not found in PATH"; exit 1; }
	RUSTFLAGS="-C relocation-model=pic" cargo build -p lynxer_$* --release \
	    --manifest-path $(LYNXER_RUST_MANIFEST) --target-dir $(LYNXER_RUST_TARGET_DIR)

define LYNXER_RUST_MODULE_RULE
$(LYNXER_DIR)/stdlib/$(1).so: $(LYNXER_RUST_TARGET_DIR)/release/liblynxer_$(1).so
	cp $$< $$@
endef
$(foreach name,$(LYNXER_RUST_MODULE_NAMES),$(eval $(call LYNXER_RUST_MODULE_RULE,$(name))))

$(LYNXER_SIGNATURE_MODULE): $(LYNXER_SIGNATURE_SOURCE)
	$(LYNXER_CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $< -o $@

# The Lynxer suite: static module/backend contract check, then the
# interpreter, compiled-executable and bundled-executable parity gates.
testLynxer: $(LYNXER_TARGET) $(LYNXER_NATIVE_BUILT) $(LYNXER_SIGNATURE_MODULE)
	@test -n "$(PYTHON)" || { echo "lynxer: python3 is required for $(LYNXER_CONTRACT_CHECK)"; exit 1; }
	@$(PYTHON) $(LYNXER_CONTRACT_CHECK)
	@$(PYTHON) $(LYNXER_GOLDEN_CHECK) --lynxer $(CLYX)
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
	@for fixture in $(LYNXER_PARITY_FIXTURES); do \
	LYNXER_GAME_HEADLESS=1 $(CLYX) $(LYNXER_DIR)/examples/$$fixture.lynx > $(CLYX_TMP)_direct.out 2>&1; \
	direct_status=$$?; \
	if ! $(CLYX) --compile $(LYNXER_DIR)/examples/$$fixture.lynx $(CLYX_TMP)_compiled > /dev/null; then \
	echo "compile failed for $$fixture (imports)"; exit 1; fi; \
	LYNXER_GAME_HEADLESS=1 $(CLYX_TMP)_compiled > $(CLYX_TMP)_compiled.out 2>&1; \
	compiled_status=$$?; \
	if ! diff -q $(CLYX_TMP)_direct.out $(CLYX_TMP)_compiled.out > /dev/null || \
	   [ "$$direct_status" -ne "$$compiled_status" ]; then \
	echo "compiled import parity failed for $$fixture"; \
	diff $(CLYX_TMP)_direct.out $(CLYX_TMP)_compiled.out | head -5; \
	exit 1; \
	fi; \
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
	@output="$$($(CLYX) $(LYNXER_NATIVE_STDLIB_FIXTURE))"; \
	expected="$$(printf '3.141592653589793\n180\ntrue\ntrue\ntrue\nlinux')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native stdlib output:"; printf '%s\n' "$$expected"; \
	echo "received native stdlib output:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(LYNXER_SIGNATURE_FIXTURE))"; \
	expected="$$(printf '7\n2.5\nzero\n7\n9\ncopy\n4\n1\n---\n6\n1.25\n4\n6.5\n4\n3.5\n1.500000\n2.75\nabcd\n3.75\nabc\nab5\nabc5\n7\nn12\n3\n9')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native signature output:"; printf '%s\n' "$$expected"; \
	echo "received native signature output:"; printf '%s\n' "$$output"; exit 1; fi
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
	LYNXER_GAME_HEADLESS=1 $(CLYX) "$$fixture" > $(CLYX_TMP)_stdlib.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "stdlib fixture failed: $$fixture"; cat $(CLYX_TMP)_stdlib.out; \
	rm -f $(CLYX_TMP)_stdlib.out; exit 1; fi; \
	if ! diff -u "$$expected" $(CLYX_TMP)_stdlib.out > $(CLYX_TMP)_stdlib.diff; then \
	echo "stdlib fixture output mismatch: $$fixture"; \
	cat $(CLYX_TMP)_stdlib.diff; \
	rm -f $(CLYX_TMP)_stdlib.out $(CLYX_TMP)_stdlib.diff; exit 1; fi; \
	done; \
	rm -f $(CLYX_TMP)_stdlib.out $(CLYX_TMP)_stdlib.diff
	@for fixture in $(LYNXER_MILESTONE7_NEW_FIXTURES) $(LYNXER_OWNERSHIP_FIXTURES) $(LYNXER_LOWLEVEL_FIXTURES); do \
	expected="$${fixture%.lynx}.expected"; \
	if [ ! -f "$$expected" ]; then \
	echo "missing expected output for $$fixture"; exit 1; fi; \
	output="$$($(CLYX) "$$fixture" 2>&1)"; \
	status=$$?; \
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
	@LYNXER_GAME_HEADLESS=1 LYNXER_SKIP_DISPLAY=$(LYNXER_SKIP_DISPLAY) $(CLYX) $(LYNXER_STDLIB_TEST_ALL) > $(CLYX_TMP)_stdlib_all.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "consolidated stdlib test failed: $(LYNXER_STDLIB_TEST_ALL)"; \
	cat $(CLYX_TMP)_stdlib_all.out; \
	rm -f $(CLYX_TMP)_stdlib_all.out; exit 1; fi; \
	rm -f $(CLYX_TMP)_stdlib_all.out
	@if [ "$(LYNXER_SKIP_DISPLAY)" = "1" ]; then \
	echo "lynxer: skipping game_clicker.lynx: display tests disabled (LYNXER_SKIP_DISPLAY=1)"; \
	else \
	LYNXER_GAME_HEADLESS=1 $(CLYX) $(LYNXER_DIR)/examples/game_clicker.lynx >/dev/null 2>&1 || \
	{ echo "example failed: $(LYNXER_DIR)/examples/game_clicker.lynx"; exit 1; }; \
	fi
	@rm -f $(CLYX_TMP)_stdin
	@echo "lynxer smoke test passed"

# Architecture-specific syscall gates. `uname -m` is asserted so the AMD64 and
# ARM64 CI jobs each run the matching fixture and a mistake fails loudly rather
# than testing the wrong architecture.
testLynxerAmd64Syscalls: $(LYNXER_TARGET)
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

testLynxerArm64Syscalls: $(LYNXER_TARGET)
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
	@rm -f $(LYNXER_NATIVE_MODULES) $(LYNXER_SIGNATURE_MODULE) $(CLYX_TMP)_*
	@rm -rf $(LYNXER_DIR)/build $(LYNXER_RUST_DIR)/target $(LYNXER_RUST_DIR)/*/target
	@echo "✓ Cleaned Lynxer build artifacts."

cleanAll: clean cleanLynxer
	@echo "✓ Cleaned all generated build artifacts."

help:
	@echo "Lynxer build targets:"
	@echo "  make build              (interpreter + native stdlib modules)"
	@echo "  make buildAll           (alias for build)"
	@echo "  make buildLynxer"
	@echo "  make buildLynxerArm64"
	@echo "  make cargo"
	@echo "  make test               (Lynxer suite)"
	@echo "  make testLynxer        (Lynxer suite only)"
	@echo "  make testLynxerAmd64Syscalls   (amd64 syscall fixture; x86_64 host)"
	@echo "  make testLynxerArm64Syscalls   (arm64 syscall fixture; aarch64 host)"
	@echo "  make check"
	@echo "  make clean"
	@echo "  make cleanLynxer"
	@echo "  make cleanAll"
	@echo "  make help"
	@echo ""
	@echo "Lynxer source commands:"
	@echo "  ./lynxer/lynxer --format <file.lynx>"
	@echo "  ./lynxer/lynxer --format-oneline <file.lynx>"
	@echo "  ./lynxer/lynxer --ast <file.lynx>"
	@echo "  ./lynxer/lynxer --lint <file.lynx>"
