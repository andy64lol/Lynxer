PYTHON   ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)
VENV     := venv
VENV_PY  := $(VENV)/bin/python
VENV_PIP := $(VENV_PY) -m pip
PYINSTALLER := $(VENV)/bin/pyinstaller

WARNING_FILE := lynxer/warnings.txt
WARNING_DATA := --add-data "$(WARNING_FILE):lynxer"

COLLECT_ALL := $(shell \
	sed 's/#.*//' requirements_venv.txt | \
	sed '/^[[:space:]]*$$/d' | \
	sed 's/[<>=!~].*//' | \
	sed 's/\[.*\]//' | \
	xargs -I{} printf -- "--collect-all=%s " "{}" \
)

CYTHON_COLLECT_ALL := --collect-all=Cython --collect-all=setuptools

SYSTEM_CALLS_DEP := system-calls
SYSTEM_CALLS := --hidden-import system_calls --hidden-import lynxer.syscalls --collect-submodules system_calls --collect-all=system_calls
NATIVE_HIDDEN_IMPORTS := --hidden-import lynxer.cpp --hidden-import lynxer.bytecode_vm

# --- Clynxer (the C++/Rust rebuild) -----------------------------------------
# Every path is repo-root relative: this Makefile owns both implementations.
CLYNXER_DIR := clynxer
CLYNXER_TARGET := $(CLYNXER_DIR)/clynxer
CLYNXER_SOURCES := $(addprefix $(CLYNXER_DIR)/,main.cpp shell.cpp lexer.cpp runtime.cpp types.cpp builtins.cpp ops.cpp ast.cpp optimizer.cpp parser.cpp config.cpp bundle.cpp interrupt.cpp)
CLYNXER_OBJECTS := $(CLYNXER_SOURCES:.cpp=.o)
CLYNXER_OBJECTS_ARM64 := $(CLYNXER_SOURCES:.cpp=.o-arm64)
CLYNXER_HEADERS := $(wildcard $(CLYNXER_DIR)/*.hpp)
CLYNXER_CXX ?= c++
CLYNXER_CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -fPIE

# Native (C++) stdlib modules: every clynxer/stdlib/<name>.cpp -> <name>.so.
CLYNXER_NATIVE_SOURCES := $(wildcard $(CLYNXER_DIR)/stdlib/*.cpp)
CLYNXER_NATIVE_MODULES := $(CLYNXER_NATIVE_SOURCES:.cpp=.so)

# Rust backends: self-contained cdylibs that the interpreter dlopens directly.
CLYNXER_RUST_DIR := $(CLYNXER_DIR)/rust
CLYNXER_RUST_TARGET_DIR := $(CLYNXER_DIR)/build/rust
CLYNXER_RUST_MANIFEST := $(CLYNXER_RUST_DIR)/Cargo.toml
CLYNXER_RUST_SOURCES := $(wildcard $(CLYNXER_RUST_DIR)/*/src/*.rs) \
                        $(wildcard $(CLYNXER_RUST_DIR)/*/Cargo.toml) \
                        $(CLYNXER_RUST_MANIFEST) $(CLYNXER_RUST_DIR)/Cargo.lock
CLYNXER_RUST_MODULE_NAMES := game image json lua network server sound sqldb tui ffi
CLYNXER_RUST_MODULES := $(CLYNXER_RUST_MODULE_NAMES:%=$(CLYNXER_DIR)/stdlib/%.so)

# Rust-backed modules are gated on the Rust toolchain.
HAVE_CARGO := $(shell command -v cargo >/dev/null 2>&1 && echo 1)
ifeq ($(HAVE_CARGO),1)
CLYNXER_RUST_BUILT := $(CLYNXER_RUST_MODULES)
else
CLYNXER_RUST_BUILT :=
endif

CLYNXER_CORE_MODULES := $(filter-out $(CLYNXER_RUST_MODULES),$(CLYNXER_NATIVE_MODULES))
CLYNXER_NATIVE_BUILT := $(CLYNXER_CORE_MODULES) $(CLYNXER_RUST_BUILT)

# Clynxer suite fixtures and static contract check.
CLYNXER_CONTRACT_CHECK := $(CLYNXER_DIR)/scripts/check_module_contracts.py
CLYNXER_GOLDEN_CHECK := $(CLYNXER_DIR)/scripts/check_golden.py
CLYNXER_CONDITION_FIXTURE := $(CLYNXER_DIR)/examples/conditions.lynx
CLYNXER_LOOP_FIXTURE := $(CLYNXER_DIR)/examples/loops.lynx
CLYNXER_ERROR_FIXTURE := $(CLYNXER_DIR)/examples/control_flow_error.lynx
CLYNXER_COMMENT_FIXTURE := $(CLYNXER_DIR)/examples/comments.lynx
CLYNXER_SETUP_ERROR_FIXTURE := $(CLYNXER_DIR)/examples/missing_setup.lynx
CLYNXER_INPUTLN_FIXTURE := $(CLYNXER_DIR)/examples/inputln.lynx
CLYNXER_MILESTONE4_FIXTURE := $(CLYNXER_DIR)/examples/milestone4.lynx
CLYNXER_MILESTONE4_PATTERN_FIXTURE := $(CLYNXER_DIR)/examples/milestone4_patterns.lynx
CLYNXER_MILESTONE5_FIXTURE := $(CLYNXER_DIR)/examples/milestone5.lynx
CLYNXER_MILESTONE5_CODEBLOCK_FIXTURE := $(CLYNXER_DIR)/examples/milestone5_codeblocks.lynx
CLYNXER_MILESTONE6_FIXTURE := $(CLYNXER_DIR)/examples/milestone6_module.lynx
CLYNXER_MILESTONE6_MATH_FIXTURE := $(CLYNXER_DIR)/examples/milestone6_math_native.lynx
CLYNXER_NATIVE_STDLIB_FIXTURE := $(CLYNXER_DIR)/examples/native_stdlibs.lynx
CLYNXER_SIGNATURE_SOURCE := $(CLYNXER_DIR)/examples/native_signatures.cpp
CLYNXER_SIGNATURE_MODULE := $(CLYNXER_DIR)/examples/native_signatures.so
CLYNXER_SIGNATURE_FIXTURE := $(CLYNXER_DIR)/examples/native_signatures.lynx
# Every clynxer/examples/stdlib_<name>.lynx must ship a sibling .expected file.
# The sound fixture is split out: although the backend tolerates a missing
# output device, several of its assertions (playback, stop, per-handle volume)
# only hold on a host with a real ALSA card. Hosts without one — CI containers,
# headless boxes — skip it and say so instead of failing.
HAVE_AUDIO := $(if $(wildcard /dev/snd/controlC*),1,)
CLYNXER_SOUND_FIXTURE := $(CLYNXER_DIR)/examples/stdlib_sound.lynx
# Display/audio tests. CI runners have neither a display nor an audio device, and
# the graphics backend crashes without a display, so the workflows pass
# CLYNXER_SKIP_DISPLAY=1 to drop the fixtures that need one.
CLYNXER_SKIP_DISPLAY ?= 0
ifeq ($(CLYNXER_SKIP_DISPLAY),1)
CLYNXER_DISPLAY_FIXTURES := stdlib_game stdlib_sound
else
CLYNXER_DISPLAY_FIXTURES :=
endif
CLYNXER_DISPLAY_FIXTURE_FILES := $(CLYNXER_DISPLAY_FIXTURES:%=$(CLYNXER_DIR)/examples/%.lynx)
CLYNXER_STDLIB_FIXTURES := $(filter-out $(CLYNXER_SOUND_FIXTURE) $(CLYNXER_DISPLAY_FIXTURE_FILES),$(wildcard $(CLYNXER_DIR)/examples/stdlib_*.lynx))
ifeq ($(HAVE_AUDIO),1)
CLYNXER_AUDIO_FIXTURES := $(filter-out $(CLYNXER_DISPLAY_FIXTURE_FILES),$(CLYNXER_SOUND_FIXTURE))
else
CLYNXER_AUDIO_FIXTURES :=
endif
# Built-in-family fixtures (clynxer/examples/builtin_<name>.lynx) do too.
CLYNXER_MILESTONE7_NEW_FIXTURES := $(CLYNXER_DIR)/examples/builtin_async.lynx $(CLYNXER_DIR)/examples/builtin_ffi.lynx
# Low-level fixtures: native-memory typed/endian access and the portable named
# syscalls. They assert only host-independent behaviour, so the same expected
# output holds on amd64 and arm64, and both CI jobs run them.
CLYNXER_LOWLEVEL_FIXTURES := $(CLYNXER_DIR)/examples/lowlevel_memory.lynx \
	$(CLYNXER_DIR)/examples/lowlevel_syscalls.lynx \
	$(CLYNXER_DIR)/examples/lowlevel_arch.lynx \
	$(CLYNXER_DIR)/examples/language_fields.lynx
# Compatibility fixture for the deprecated symbolic operator spellings. It is
# diffed like a stdlib fixture and also compiled, so the old spellings keep
# working through both the interpreter and --compile.
CLYNXER_DEPRECATED_FIXTURES := $(CLYNXER_DIR)/examples/deprecated_operators.lynx
# Single self-checking test that exercises every stdlib module at once.
CLYNXER_STDLIB_TEST_ALL := $(CLYNXER_DIR)/examples/stdlibTestAll.lynx
# AST optimizer: fixture plus its expected stdout+stderr, so an optimized run,
# a --no-opt run and the report all have something to compare against.
CLYNXER_OPTIMIZER_FIXTURE := $(CLYNXER_DIR)/examples/optimizer.lynx
CLYNXER_OPTIMIZER_EXPECTED := $(CLYNXER_DIR)/examples/optimizer.expected
CLYNXER_OPTIMIZER_DEPRECATED_FIXTURE := $(CLYNXER_DIR)/examples/optimizer_deprecated.lynx
CLYNXER_LIST_STDLIB_MODULES := cli colorlib csv debug fileIO game image js json lua math \
	multiprocessing network os path random re regex server shell sound sqldb sys text time tui typing
# Import-parity fixtures (interpreted vs compiled). The sound one needs a device.
CLYNXER_PARITY_FIXTURES := native_stdlibs milestone6_module milestone6_math_native stdlib_json \
	stdlib_re stdlib_path stdlib_game stdlib_image stdlib_lua stdlib_sqldb stdlib_tui deprecated_operators optimizer \
	lowlevel_memory lowlevel_syscalls lowlevel_arch language_fields
ifeq ($(HAVE_AUDIO),1)
CLYNXER_PARITY_FIXTURES += stdlib_sound
endif
CLYNXER_PARITY_FIXTURES := $(filter-out $(CLYNXER_DISPLAY_FIXTURES),$(CLYNXER_PARITY_FIXTURES))

# Recipe shorthands: the interpreter, and the temp-file prefix for the suite.
CLYX := ./$(CLYNXER_TARGET)
CLYX_TMP := $(CLYNXER_DIR)/.clynxer

.PHONY: all venv deps liteDeps pyinstaller cargo platform-check lite-platform-check build buildAll buildLynxer buildLynxerLite buildCpp buildCLynxer buildCLynxerArm64 test testLynxer testCLynxer testAMR64 validate golden check clean cleanC cleanCpp cleanLynxc cleanCLynxer cleanAll help

venv:
	@if [ ! -d "$(VENV)" ]; then \
		echo "Creating virtual environment '$(VENV)'..."; \
		$(PYTHON) -m venv $(VENV); \
	else \
		echo "Using existing virtual environment '$(VENV)'."; \
	fi
	@echo "Upgrading pip and setuptools..."
	@$(VENV_PIP) install --upgrade pip setuptools

deps: venv
	@echo "Installing full dependencies..."
	@$(VENV_PIP) install --upgrade -r requirements_venv.txt

	@echo "Installing the Linux syscall tables..."
	@$(VENV_PIP) install --upgrade $(SYSTEM_CALLS_DEP)

liteDeps: venv
	@echo "Installing Lite dependencies..."
	@$(VENV_PIP) install --upgrade cython setuptools

	@echo "Installing the Linux syscall tables..."
	@$(VENV_PIP) install --upgrade $(SYSTEM_CALLS_DEP)

platform-check: deps
	@echo "Checking Linux build platform..."
	@$(VENV_PY) -c 'from lynxer.syscalls import require_supported_platform, WORD_BYTES; architecture = require_supported_platform(); print(f"  -> {architecture} ({WORD_BYTES * 8}-bit Python ABI)")'

lite-platform-check: liteDeps
	@echo "Checking Linux build platform..."
	@$(VENV_PY) -c 'from lynxer.syscalls import require_supported_platform, WORD_BYTES; architecture = require_supported_platform(); print(f"  -> {architecture} ({WORD_BYTES * 8}-bit Python ABI)")'

test: testLynxer testCLynxer

# Python-only suite: the Clynxer suite below is gated on a Rust toolchain, so
# CI jobs that only install Python use this target instead of `test`.
testLynxer: buildCpp
	@echo "Running Lynxer tests..."
	@$(VENV_PY) -u test/validate.py
	@$(VENV_PY) -u test/remaining.py

testAMR64: buildCpp
	@echo "Running ARM64 syscall database test..."
	@$(VENV_PY) -u scripts/testARM64Syscall.py

validate: buildCpp
	@echo "Running validation..."
	@$(VENV_PY) -u lynxer/validate.py

golden:
	@echo "Checking the Stage 1 golden corpus..."
	@$(PYTHON) -u scripts/golden_corpus.py

check: test
	@for file in syntax.lynx test/*.lynx; do \
		if grep -q '^// EXPECT_ERROR:' "$$file"; then \
			echo "Skipping expected-error fixture: $$file"; \
			continue; \
		fi; \
		$(VENV_PY) lynxer/shell.py --lint "$$file" >/dev/null || exit $$?; \
	done
	@echo "✓ Lynxer checks passed."

# Conventional alias for a full build (kept out of first position so a bare
# `make` does not trigger the ~13-minute PyInstaller lite analysis).
all: build

# Everything: both Python binaries plus Clynxer.
build: buildLynxer buildLynxerLite buildCLynxer
	@echo "✓ Full build complete: dist/lynxer, dist/lynxer-lite, $(CLYNXER_TARGET)"

buildAll: build

# Shared Python-build prerequisite: PyInstaller plus the Arcade hook patch.
pyinstaller: venv
	@echo "Patching Arcade PyInstaller hook... (due to a bug)"
	@HOOK=$$($(VENV_PY) -c 'import arcade, os; print(os.path.join(os.path.dirname(arcade.__file__), "__pyinstaller", "hook-arcade.py"))'); \
	if [ -f "$$HOOK" ]; then \
		sed -i.bak 's|"./arcade/VERSION"|"./arcade"|g' "$$HOOK"; \
		rm -f "$$HOOK.bak"; \
		echo "  -> Patched $$HOOK"; \
	else \
		echo "  -> Hook file not found (Arcade may not be installed?)"; \
	fi
	@echo "Installing PyInstaller..."
	@$(VENV_PIP) install --upgrade pyinstaller

# Python full build: every stdlib module bundled into dist/lynxer.
buildLynxer: platform-check buildCpp pyinstaller
	@echo "Building Lynxer (Python)..."
	@$(PYINSTALLER) \
		--onefile \
		--clean \
		$(COLLECT_ALL) \
		--name lynxer \
		$(NATIVE_HIDDEN_IMPORTS) \
		$(SYSTEM_CALLS) \
		$(WARNING_DATA) \
		--add-data "lynxer/stdlib:stdlib" \
		lynxer/shell.py
	@echo "✓ Lynxer build complete: dist/lynxer"

# Python lite build: Cython support and only pure stdlib modules, dist/lynxer-lite.
buildLynxerLite: lite-platform-check buildCpp pyinstaller
	@echo "Selecting pure stdlib .lynx modules..."
	@rm -rf build/stdlib_pure || true
	@$(VENV_PY) scripts/select_pure_stdlib.py lynxer/stdlib build/stdlib_pure

	@echo "Building Lynxer (lite) with Cython support and only pure stdlib modules..."
	@$(PYINSTALLER) \
		--onefile \
		--clean \
		$(CYTHON_COLLECT_ALL) \
		--hidden-import Cython.Build.Inline \
		--name lynxer-lite \
		$(NATIVE_HIDDEN_IMPORTS) \
		$(SYSTEM_CALLS) \
		$(WARNING_DATA) \
		--add-data "build/stdlib_pure:stdlib" \
		lynxer/shell.py
	@echo "✓ Lite build complete: dist/lynxer-lite"

buildCpp: venv
	@echo "Building Lynxer C++ native extensions..."
	@$(VENV_PY) lynxer/setup.py build_ext --inplace
	@echo "✓ Native extensions built in lynxer/ (memory + bytecode VM)"

# ---------------------------------------------------------------------------
# Clynxer: the C++ interpreter, its native (C++) stdlib modules, and the Rust
# stdlib backends. All paths are repo-root relative.
# ---------------------------------------------------------------------------

# Binary plus every stdlib module (C++, and the Rust ones when cargo exists).
buildCLynxer: $(CLYNXER_TARGET) $(CLYNXER_NATIVE_BUILT)
	@echo "✓ Clynxer build complete: $(CLYNXER_TARGET)"

# ARM64 (aarch64) binary. Requires aarch64-linux-gnu-g++ installed.
buildCLynxerArm64: $(CLYNXER_TARGET)-arm64 $(CLYNXER_NATIVE_BUILT)
	@echo "✓ Clynxer ARM64 build complete: $(CLYNXER_TARGET)-arm64"

# Rust backends only. Skipped with a message when cargo is not installed.
ifeq ($(HAVE_CARGO),1)
cargo: $(CLYNXER_RUST_MODULES)
	@echo "✓ Clynxer Rust backends ready ($(CLYNXER_RUST_TARGET_DIR))"
else
cargo:
	@echo "clynxer: skipping the Rust backends: cargo not found in PATH"
endif

$(CLYNXER_TARGET): $(CLYNXER_OBJECTS)
	$(CLYNXER_CXX) $(CLYNXER_CXXFLAGS) $(CLYNXER_OBJECTS) -o $@

$(CLYNXER_TARGET)-arm64: $(CLYNXER_OBJECTS_ARM64)
	@command -v aarch64-linux-gnu-g++ >/dev/null || { echo "error: aarch64-linux-gnu-g++ not found"; exit 1; }
	@aarch64-linux-gnu-g++ $(CLYNXER_CXXFLAGS) $(filter %.o-arm64,$^) -o $@ -static-libstdc++ -static-libgcc

$(CLYNXER_DIR)/%.o: $(CLYNXER_DIR)/%.cpp $(CLYNXER_HEADERS)
	$(CLYNXER_CXX) $(CLYNXER_CXXFLAGS) -c $< -o $@

$(CLYNXER_DIR)/%.o-arm64: $(CLYNXER_DIR)/%.cpp $(CLYNXER_HEADERS)
	@command -v aarch64-linux-gnu-g++ >/dev/null || { echo "error: aarch64-linux-gnu-g++ not found"; exit 1; }
	@aarch64-linux-gnu-g++ $(CLYNXER_CXXFLAGS) -c $< -o $@

# Remaining C++ stdlib modules, compiled to clynxer/stdlib/<name>.so.
$(CLYNXER_DIR)/stdlib/%.so: $(CLYNXER_DIR)/stdlib/%.cpp
	$(CLYNXER_CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $< -o $@

# Rust backends: self-contained cdylibs exporting lynxer_module_init_v1 and
# their ops, copied to clynxer/stdlib/<name>.so for the interpreter to dlopen.
$(CLYNXER_RUST_TARGET_DIR)/release/libclynxer_%.so: $(CLYNXER_RUST_SOURCES)
	@command -v cargo >/dev/null || { echo "clynxer: cargo not found in PATH"; exit 1; }
	RUSTFLAGS="-C relocation-model=pic" cargo build -p clynxer_$* --release \
	    --manifest-path $(CLYNXER_RUST_MANIFEST) --target-dir $(CLYNXER_RUST_TARGET_DIR)

define CLYNXER_RUST_MODULE_RULE
$(CLYNXER_DIR)/stdlib/$(1).so: $(CLYNXER_RUST_TARGET_DIR)/release/libclynxer_$(1).so
	cp $$< $$@
endef
$(foreach name,$(CLYNXER_RUST_MODULE_NAMES),$(eval $(call CLYNXER_RUST_MODULE_RULE,$(name))))

$(CLYNXER_SIGNATURE_MODULE): $(CLYNXER_SIGNATURE_SOURCE)
	$(CLYNXER_CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $< -o $@

# The Clynxer suite: static module/backend contract check, then the
# interpreter, compiled-executable and bundled-executable parity gates.
testCLynxer: $(CLYNXER_TARGET) $(CLYNXER_NATIVE_BUILT) $(CLYNXER_SIGNATURE_MODULE)
	@test -n "$(PYTHON)" || { echo "clynxer: python3 is required for $(CLYNXER_CONTRACT_CHECK)"; exit 1; }
	@$(PYTHON) $(CLYNXER_CONTRACT_CHECK)
	@$(PYTHON) $(CLYNXER_GOLDEN_CHECK) --clynxer $(CLYX)
	@printf 'Clynxer\n' > $(CLYX_TMP)_stdin
	@output="$$($(CLYX) $(CLYNXER_DIR)/examples/hello.lynx < $(CLYX_TMP)_stdin)"; \
	case "$$output" in \
	*"Hello, Clynxer!") ;; \
	*) echo "expected greeting 'Hello, Clynxer!', received: $$output"; exit 1;; \
	esac
	@output="$$($(CLYX) $(CLYNXER_CONDITION_FIXTURE))"; \
	expected="$$(printf 'if branch\nelse branch')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected condition output:"; \
	printf '%s\n' "$$expected"; \
	echo "received condition output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(CLYNXER_COMMENT_FIXTURE))"; \
	expected="$$(printf '3\n/// this is string content, not a comment')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected comment output:"; \
	printf '%s\n' "$$expected"; \
	echo "received comment output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(CLYNXER_LOOP_FIXTURE))"; \
	expected="$$(printf 'while 0\nwhile 2\nfor 0\nfor 1\ndoWhile 1\ndoWhile 3\niterate 0\niterate 1\nforever 2')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected loop output:"; \
	printf '%s\n' "$$expected"; \
	echo "received loop output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(CLYNXER_INPUTLN_FIXTURE) < $(CLYX_TMP)_stdin)"; \
	expected="$$(printf 'Hello, world!\nClynxer')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected inputln output:"; printf '%s\n' "$$expected"; \
	echo "received inputln output:"; printf '%s\n' "$$output"; exit 1; fi
	@set +e; \
	output="$$($(CLYX) $(CLYNXER_SETUP_ERROR_FIXTURE) 2>&1)"; \
	status=$$?; \
	set -e; \
expected="clynxer: $(CLYNXER_SETUP_ERROR_FIXTURE):3:2: program must define global setup()"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected setup enforcement: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(CLYNXER_MILESTONE4_FIXTURE))"; \
	expected="$$(printf "2\\n7\\n5\\n-3\\n-6\\n-8\\n-7\\n12\\n4\\n32\\n-3\\nfalse\\ntrue\\ntrue\\nelif\\ncase\\ncaught: Cannot convert 'not an integer' to int\\n\\033")"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone4 output:"; printf '%s\n' "$$expected"; \
	echo "received milestone4 output:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(CLYNXER_MILESTONE4_PATTERN_FIXTURE))"; \
	if [ "$$output" != "2" ]; then \
	echo "expected milestone4 pattern output: 2"; \
	echo "received: $$output"; exit 1; fi
	@set +e; \
	output="$$($(CLYX) $(CLYNXER_ERROR_FIXTURE) 2>&1)"; \
	status=$$?; \
	set -e; \
expected="clynxer: $(CLYNXER_ERROR_FIXTURE):4:8: unknown variable 'missing'"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected source-located error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(CLYNXER_DIR)/examples/builtins.lynx)"; \
	expected="$$(printf '99 3.14 true\n42 3\n1.5\n[0, 1, 2, 3, 4]\n[2, 3, 4, 5, 6, 7]\n[0, 2, 4, 6, 8]\n[10, 9, 8, 7, 6, 5, 4, 3, 2, 1]\n[]\n[0, 2, 4, 6, 8]\n[5, 3, 9, 1, 7]\n7 [5, 3, 9, 1, 7]\n7\n[100, 3, 9, 1, 7]\n[3, 9]\ntrue false\n2 -1\n5-3-9-1-7\ntrue true false\n25 5\n[1, 3, 5, 7, 9] [9, 7, 5, 3, 1]\n[7, 1, 9, 3, 5]\n1 9\n[5, 3] [1, 7]\n5 7\n1\n[5, 3, 9, 1, 7, 8, 8]\n[5, 50, 3, 9, 1, 7]\n[1, 2, 3, 4]\n[8]\n[x, x, x]\n[{"a": 1, "b": "a"}, {"a": 2, "b": "b"}]\n[1, "two", true, null]\n{"name": "Ada", "age": 36}\n[a, b, c]\ntrue\n(10, 20) (1, two, 3)\n3 two 3\ntrue 0\n(10,)\n[10, 20] (4, 5)\n(10, 20, 1, two, 3)\n1\n1 3\n[10, 20]\n(20, 10)\n(1, 2, 3) (3, 2, 1)\n10 20 30\ntrue true\n(1, 2)\n3\n({"a": 1, "b": "a"}, {"a": 2, "b": "b"})\n10-20\nint float str\nbool none list\n5 5\nMISSING sentinel <sentinel>\n<object> object\n42\n-7\n200\n305419896\n4 8 2\nHello, Ada!\nAda is 36\n40\nno {name} interpolation here\n3')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected builtins output:"; \
	printf '%s\n' "$$expected"; \
	echo "received builtins output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@for fixture in hello conditions loops comments builtins milestone4 milestone5 milestone5_codeblocks; do \
	$(CLYX) $(CLYNXER_DIR)/examples/$$fixture.lynx < $(CLYX_TMP)_stdin > $(CLYX_TMP)_direct.out 2>$(CLYX_TMP)_direct.err; \
	direct_status=$$?; \
	if ! $(CLYX) --compile $(CLYNXER_DIR)/examples/$$fixture.lynx $(CLYX_TMP)_compiled > /dev/null; then \
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
	@for fixture in $(CLYNXER_PARITY_FIXTURES); do \
	CLYNXER_GAME_HEADLESS=1 $(CLYX) $(CLYNXER_DIR)/examples/$$fixture.lynx > $(CLYX_TMP)_direct.out 2>&1; \
	direct_status=$$?; \
	if ! $(CLYX) --compile $(CLYNXER_DIR)/examples/$$fixture.lynx $(CLYX_TMP)_compiled > /dev/null; then \
	echo "compile failed for $$fixture (imports)"; exit 1; fi; \
	CLYNXER_GAME_HEADLESS=1 $(CLYX_TMP)_compiled > $(CLYX_TMP)_compiled.out 2>&1; \
	compiled_status=$$?; \
	if ! diff -q $(CLYX_TMP)_direct.out $(CLYX_TMP)_compiled.out > /dev/null || \
	   [ "$$direct_status" -ne "$$compiled_status" ]; then \
	echo "compiled import parity failed for $$fixture"; \
	diff $(CLYX_TMP)_direct.out $(CLYX_TMP)_compiled.out | head -5; \
	exit 1; \
	fi; \
	done
	@$(CLYX) --bundle $(CLYNXER_DIR)/examples/hello.lynx $(CLYX_TMP)_bundled > /dev/null; \
	bundled_output="$$($(CLYX_TMP)_bundled < $(CLYX_TMP)_stdin)"; \
	case "$$bundled_output" in \
	*"Hello, Clynxer!") ;; \
	*) echo "bundled executable output mismatch: $$bundled_output"; exit 1;; \
	esac
	@$(CLYX) --compile $(CLYNXER_DIR)/examples/bundle_app.lynx $(CLYNXER_DIR)/examples/bundle_extras/greeter.lynx $(CLYNXER_DIR)/examples/bundle_extras/counter.lynx -o $(CLYX_TMP)_multi > /dev/null; \
	multi_output="$$($(CLYX_TMP)_multi)"; \
	expected="$$(printf 'hello, LYNXER!\n1,2,3')"; \
	if [ "$$multi_output" != "$$expected" ]; then \
	echo "multi-file bundle output mismatch:"; printf '%s\n' "$$multi_output"; exit 1; fi
	@$(CLYX) --compile $(CLYNXER_DIR)/examples/bundle_assets.lynx --include $(CLYNXER_DIR)/examples/bundle_extras/message.txt -o $(CLYX_TMP)_assets > /dev/null; \
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
	@output="$$($(CLYX) $(CLYNXER_DIR)/examples/milestone3.lynx)"; \
	expected="$$(printf '9\nAda\n90\n5\nEngineer\n6\n42\n1\n0\n255\n-12\n0.25\nA\n[1, 2, 3]\n(left, 2)')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone3 output:"; \
	printf '%s\n' "$$expected"; \
	echo "received milestone3 output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(CLYNXER_DIR)/examples/m3_scalars.lynx)"; \
	expected="$$(printf '3.5\nZ\n1\n0\n255\n-128\n-300\n100000\n5000000000\n200\n60000\n3000000000\n9000000000\n1.5\n2.25\n[1, 2, 3]\n(10, 20)\n42\n9')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected m3_scalars output:"; \
	printf '%s\n' "$$expected"; \
	echo "received m3_scalars output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(CLYNXER_DIR)/examples/m3_scope.lynx)"; \
	expected="$$(printf '1')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected m3_scope output (flat per-function scope):"; \
	printf '%s\n' "$$expected"; \
	echo "received m3_scope output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@set +e; \
	output="$$($(CLYX) $(CLYNXER_DIR)/examples/m3_errors.lynx 2>&1)"; \
	status=$$?; \
	set -e; \
	expected="clynxer: $(CLYNXER_DIR)/examples/m3_errors.lynx:4:5: value 200 is out of range for type 'int8'"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected out-of-range error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@set +e; \
	output="$$($(CLYX) $(CLYNXER_DIR)/examples/m3_const.lynx 2>&1)"; \
	status=$$?; \
	set -e; \
	expected="clynxer: $(CLYNXER_DIR)/examples/m3_const.lynx:5:5: variable 'frozen' is constant and cannot be reassigned"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected const reassignment error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@set +e; \
	output="$$($(CLYX) $(CLYNXER_DIR)/examples/m3_vargroup.lynx 2>&1)"; \
	status=$$?; \
	set -e; \
	expected="clynxer: $(CLYNXER_DIR)/examples/m3_vargroup.lynx:8:5: Vargroup and legacy class-field assignment requires an explicit type"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected vargroup explicit-type error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@output="$$($(CLYX) $(CLYNXER_MILESTONE5_FIXTURE))"; \
	expected="$$(printf '5\n7\n25')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone5 output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(CLYNXER_MILESTONE5_CODEBLOCK_FIXTURE))"; \
	expected="$$(printf 'one\ntwo\nsaved\nafter')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone5 codeblock output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(CLYNXER_MILESTONE6_FIXTURE))"; \
	expected="$$(printf '6\n42\n6\n6\n7\n\033[31mok\033[0m\ntrue\ncba\n4')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone6 output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(CLYNXER_MILESTONE6_MATH_FIXTURE))"; \
	expected="$$(printf '3\n0\ntrue\ntrue\ntrue is now a string.\n42 is a integer.\nint is the type of number.')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native math output:"; printf '%s\n' "$$expected"; \
	echo "received native math output:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(CLYNXER_NATIVE_STDLIB_FIXTURE))"; \
	expected="$$(printf '3.141592653589793\n180\ntrue\ntrue\ntrue\nlinux')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native stdlib output:"; printf '%s\n' "$$expected"; \
	echo "received native stdlib output:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$($(CLYX) $(CLYNXER_SIGNATURE_FIXTURE))"; \
	expected="$$(printf '7\n2.5\nzero\n7\n9\ncopy\n4\n1\n---\n6\n1.25\n4\n6.5\n4\n3.5\n1.500000\n2.75\nabcd\n3.75\nabc\nab5\nabc5\n7\nn12\n3\n9')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native signature output:"; printf '%s\n' "$$expected"; \
	echo "received native signature output:"; printf '%s\n' "$$output"; exit 1; fi
	@list_output="$$(cd /tmp && "$(CURDIR)/$(CLYNXER_TARGET)" --list-stdlibs)"; \
	for module in $(CLYNXER_LIST_STDLIB_MODULES); do \
	if ! printf '%s\n' "$$list_output" | grep -Fqx "  $$module"; then \
	echo "stdlib listing is missing module: $$module"; exit 1; fi; \
	done
	@if [ -z "$(HAVE_AUDIO)" ]; then \
	echo "clynxer: skipping $(notdir $(CLYNXER_SOUND_FIXTURE)): no audio device (/dev/snd/controlC*) on this host"; fi
	@for fixture in $(CLYNXER_STDLIB_FIXTURES) $(CLYNXER_AUDIO_FIXTURES) $(CLYNXER_DEPRECATED_FIXTURES); do \
	expected="$${fixture%.lynx}.expected"; \
	if [ ! -f "$$expected" ]; then \
	echo "missing expected output for $$fixture"; exit 1; fi; \
	CLYNXER_GAME_HEADLESS=1 $(CLYX) "$$fixture" > $(CLYX_TMP)_stdlib.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "stdlib fixture failed: $$fixture"; cat $(CLYX_TMP)_stdlib.out; \
	rm -f $(CLYX_TMP)_stdlib.out; exit 1; fi; \
	if ! diff -u "$$expected" $(CLYX_TMP)_stdlib.out > $(CLYX_TMP)_stdlib.diff; then \
	echo "stdlib fixture output mismatch: $$fixture"; \
	cat $(CLYX_TMP)_stdlib.diff; \
	rm -f $(CLYX_TMP)_stdlib.out $(CLYX_TMP)_stdlib.diff; exit 1; fi; \
	done; \
	rm -f $(CLYX_TMP)_stdlib.out $(CLYX_TMP)_stdlib.diff
	@for fixture in $(CLYNXER_MILESTONE7_NEW_FIXTURES) $(CLYNXER_LOWLEVEL_FIXTURES); do \
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
	@$(CLYX) $(CLYNXER_OPTIMIZER_FIXTURE) > $(CLYX_TMP)_opt.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "optimizer fixture failed: $(CLYNXER_OPTIMIZER_FIXTURE)"; \
	cat $(CLYX_TMP)_opt.out; rm -f $(CLYX_TMP)_opt.out; exit 1; fi; \
	if ! diff -u $(CLYNXER_OPTIMIZER_EXPECTED) $(CLYX_TMP)_opt.out; then \
	echo "optimizer fixture output mismatch"; rm -f $(CLYX_TMP)_opt.out; exit 1; fi
	@$(CLYX) --no-opt $(CLYNXER_OPTIMIZER_FIXTURE) > $(CLYX_TMP)_noopt.out 2>&1; \
	if ! diff -u $(CLYX_TMP)_opt.out $(CLYX_TMP)_noopt.out; then \
	echo "optimized and --no-opt runs of the optimizer fixture differ"; \
	rm -f $(CLYX_TMP)_opt.out $(CLYX_TMP)_noopt.out; exit 1; fi; \
	rm -f $(CLYX_TMP)_opt.out $(CLYX_TMP)_noopt.out
	@CLYNXER_OPT_REPORT=1 $(CLYX) $(CLYNXER_OPTIMIZER_FIXTURE) > /dev/null 2> $(CLYX_TMP)_opt.report; \
	if ! grep -Eq 'constant folds=[1-9][0-9]*' $(CLYX_TMP)_opt.report || \
	   ! grep -Eq 'short-circuits=[1-9][0-9]*' $(CLYX_TMP)_opt.report || \
	   ! grep -Eq 'dead branches=[1-9][0-9]*' $(CLYX_TMP)_opt.report; then \
	echo "optimizer did not exercise every transformation class:"; \
	cat $(CLYX_TMP)_opt.report; rm -f $(CLYX_TMP)_opt.report; exit 1; fi; \
	rm -f $(CLYX_TMP)_opt.report
	@$(CLYX) $(CLYNXER_OPTIMIZER_DEPRECATED_FIXTURE) > $(CLYX_TMP)_dep.out 2> $(CLYX_TMP)_dep.err; \
	if [ "$$(cat $(CLYX_TMP)_dep.out)" != "1" ]; then \
	echo "deprecated operator result changed under optimization"; \
	rm -f $(CLYX_TMP)_dep.out $(CLYX_TMP)_dep.err; exit 1; fi; \
	if ! grep -q "operator '&' is deprecated" $(CLYX_TMP)_dep.err; then \
	echo "optimizer swallowed the deprecation warning"; \
	rm -f $(CLYX_TMP)_dep.out $(CLYX_TMP)_dep.err; exit 1; fi; \
	rm -f $(CLYX_TMP)_dep.out $(CLYX_TMP)_dep.err
	@CLYNXER_GAME_HEADLESS=1 CLYNXER_SKIP_DISPLAY=$(CLYNXER_SKIP_DISPLAY) $(CLYX) $(CLYNXER_STDLIB_TEST_ALL) > $(CLYX_TMP)_stdlib_all.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "consolidated stdlib test failed: $(CLYNXER_STDLIB_TEST_ALL)"; \
	cat $(CLYX_TMP)_stdlib_all.out; \
	rm -f $(CLYX_TMP)_stdlib_all.out; exit 1; fi; \
	rm -f $(CLYX_TMP)_stdlib_all.out
	@if [ "$(CLYNXER_SKIP_DISPLAY)" = "1" ]; then \
	echo "clynxer: skipping game_clicker.lynx: display tests disabled (CLYNXER_SKIP_DISPLAY=1)"; \
	else \
	CLYNXER_GAME_HEADLESS=1 $(CLYX) $(CLYNXER_DIR)/examples/game_clicker.lynx >/dev/null 2>&1 || \
	{ echo "example failed: $(CLYNXER_DIR)/examples/game_clicker.lynx"; exit 1; }; \
	fi
	@rm -f $(CLYX_TMP)_stdin
	@echo "clynxer smoke test passed"

clean:
	@find . -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null || true
	@find . -name '*.pyc' -delete 2>/dev/null || true
	@find . -name '*.lynxc' -not -path '*/stdlib/*' -delete 2>/dev/null || true
	@rm -rf build dist *.spec lynxer/build 2>/dev/null || true
	@echo "✓ Cleaned."
	@echo "  (kept stdlib/*.lynxc and the built native extensions;"
	@echo "   run 'make cleanLynxc' or 'make cleanC' to remove those)"

cleanC:
	@rm -rf lynxer/build 2>/dev/null || true
	@find lynxer -maxdepth 1 -name '*.so' -delete 2>/dev/null || true
	@echo "✓ Cleaned the native extensions (lynxer/*.so)."

cleanCpp: cleanC

cleanLynxc:
	@find . -name '*.lynxc' -delete 2>/dev/null || true
	@echo "✓ Cleaned compiled bytecode (*.lynxc)."

cleanCLynxer:
	@rm -f $(CLYNXER_TARGET) $(CLYNXER_TARGET)-arm64 $(CLYNXER_OBJECTS) $(CLYNXER_OBJECTS_ARM64)
	@rm -f $(CLYNXER_NATIVE_MODULES) $(CLYNXER_SIGNATURE_MODULE) $(CLYX_TMP)_*
	@rm -rf $(CLYNXER_DIR)/build $(CLYNXER_RUST_DIR)/target $(CLYNXER_RUST_DIR)/*/target
	@echo "✓ Cleaned Clynxer build artifacts."

cleanAll: clean cleanC cleanLynxc cleanCLynxer
	@echo "✓ Cleaned all generated build artifacts."

help:
	@echo "Lynxer build targets:"
	@echo "  make build              (everything: Python full + lite + Clynxer)"
	@echo "  make buildAll           (alias for build)"
	@echo "  make buildLynxer        (Python full -> dist/lynxer)"
	@echo "  make buildLynxerLite    (Python lite -> dist/lynxer-lite)"
	@echo "  make buildCpp"
	@echo "  make buildCLynxer"
	@echo "  make buildCLynxerArm64"
	@echo "  make cargo"
	@echo "  make platform-check"
	@echo "  make venv"
	@echo "  make deps"
	@echo "  make liteDeps"
	@echo "  make test               (everything: Lynxer + Clynxer suites)"
	@echo "  make testLynxer         (Python suite only)"
	@echo "  make testCLynxer        (Clynxer suite only)"
	@echo "  make testAMR64"
	@echo "  make check"
	@echo "  make golden"
	@echo "  make clean"
	@echo "  make cleanC"
	@echo "  make cleanCpp"
	@echo "  make cleanLynxc"
	@echo "  make cleanCLynxer"
	@echo "  make cleanAll"
	@echo "  make help"
	@echo ""
	@echo "Lynxer source commands:"
	@echo "  lynxer --format <file.lynx>"
	@echo "  lynxer --format-oneline <file.lynx>"
	@echo "  lynxer --ast <file.lynx>"
	@echo "  lynxer --lint <file.lynx>"
