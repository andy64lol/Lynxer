CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -fPIE

# Static check comparing every stdlib/<name>.lynx wrapper with its backend.
# The fixture suite cannot catch a backend that disagrees with its own wrapper,
# because a fixture records what the code does; see the script's docstring.
PYTHON ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null)
CONTRACT_CHECK := scripts/check_module_contracts.py

TARGET := clynxer
NATIVE_SOURCES := $(wildcard stdlib/*.cpp)
NATIVE_MODULES := $(NATIVE_SOURCES:.cpp=.so)

# Rust backends: self-contained cdylibs that the interpreter dlopens directly.
RUST_DIR := rust
RUST_TARGET_DIR := build/rust
RUST_MANIFEST := $(RUST_DIR)/Cargo.toml
RUST_SOURCES := $(wildcard $(RUST_DIR)/*/src/*.rs) \
                $(wildcard $(RUST_DIR)/*/Cargo.toml) $(RUST_MANIFEST) \
                $(RUST_DIR)/Cargo.lock
RUST_MODULE_NAMES := game image json lua network server sound sqldb tui ffi
RUST_MODULES := $(RUST_MODULE_NAMES:%=stdlib/%.so)

# Rust-backed modules are gated on the Rust toolchain.
HAVE_CARGO := $(shell command -v cargo >/dev/null 2>&1 && echo 1)
ifeq ($(HAVE_CARGO),1)
RUST_BUILT := $(RUST_MODULES)
else
RUST_BUILT :=
$(warning clynxer: skipping Rust modules ($(RUST_MODULE_NAMES)): cargo not found in PATH)
endif

CORE_MODULES := $(filter-out $(RUST_MODULES),$(NATIVE_MODULES))
NATIVE_BUILT := $(CORE_MODULES) $(RUST_BUILT)

SOURCES := main.cpp shell.cpp lexer.cpp runtime.cpp types.cpp builtins.cpp ops.cpp ast.cpp parser.cpp config.cpp bundle.cpp interrupt.cpp
HEADERS := $(wildcard *.hpp)
OBJECTS := $(SOURCES:.cpp=.o)
CONDITION_FIXTURE := examples/conditions.lynx
LOOP_FIXTURE := examples/loops.lynx
ERROR_FIXTURE := examples/control_flow_error.lynx
COMMENT_FIXTURE := examples/comments.lynx
SETUP_ERROR_FIXTURE := examples/missing_setup.lynx
INPUTLN_FIXTURE := examples/inputln.lynx
MILESTONE4_FIXTURE := examples/milestone4.lynx
MILESTONE4_PATTERN_FIXTURE := examples/milestone4_patterns.lynx
MILESTONE5_FIXTURE := examples/milestone5.lynx
MILESTONE5_CODEBLOCK_FIXTURE := examples/milestone5_codeblocks.lynx
MILESTONE6_FIXTURE := examples/milestone6_module.lynx
MILESTONE6_MATH_FIXTURE := examples/milestone6_math_native.lynx
NATIVE_STDLIB_FIXTURE := examples/native_stdlibs.lynx
NATIVE_SIGNATURE_SOURCE := examples/native_signatures.cpp
NATIVE_SIGNATURE_MODULE := examples/native_signatures.so
NATIVE_SIGNATURE_FIXTURE := examples/native_signatures.lynx
# Every examples/stdlib_<name>.lynx must ship a sibling .expected file.
STDLIB_FIXTURES := $(wildcard examples/stdlib_*.lynx)
# Built-in-family fixtures (examples/builtin_<name>.lynx) do too.
BUILTIN_FIXTURES := $(wildcard examples/builtin_*.lynx)
MILESTONE7_NEW_FIXTURES := examples/builtin_async.lynx examples/builtin_ffi.lynx
# Single self-checking test that exercises every stdlib module at once.
STDLIB_TEST_ALL := examples/stdlibTestAll.lynx
LIST_STDLIB_MODULES := cli colorlib csv debug fileIO game image js json lua math \
	multiprocessing network os path random re regex server shell sound sqldb sys text time tui typing

.PHONY: all clean test rust game

all: $(TARGET) $(NATIVE_BUILT)

# Build every Rust backend (`rust`) or just the game module (`game`). Skipped
# with a message when cargo is not installed.
ifeq ($(HAVE_CARGO),1)
rust: $(RUST_MODULES)
game: stdlib/game.so
else
rust:
	@echo "clynxer: skipping the Rust backends: cargo not found in PATH"
game:
	@echo "clynxer: skipping stdlib/game.so: cargo not found in PATH"
endif

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

# ARM64 target: cross-compiled binary for aarch64
$(TARGET)-arm64: $(OBJECTS:.o=.o-arm64)
	@command -v aarch64-linux-gnu-g++ >/dev/null || { echo "error: aarch64-linux-gnu-g++ not found"; exit 1; }
	@aarch64-linux-gnu-g++ $(CXXFLAGS) $(filter %.o-arm64,$^) -o $@ -static-libstdc++ -static-libgcc

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Remaining C++ stdlib modules, compiled to stdlib/<name>.so.
stdlib/%.so: stdlib/%.cpp
	$(CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $< -o $@

# --- C++ compilation rules ----------------------------------------------------

# C++ source → object files. Uses the system compiler (not Nix).
clynxer/%.o: clynxer/%.cpp $(HEADERS)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# ARM64 build: cross-compile for aarch64 using aarch64-linux-gnu-g++
clynxer/%.o-arm64: clynxer/%.cpp $(HEADERS)
	@command -v aarch64-linux-gnu-g++ >/dev/null || { echo "error: aarch64-linux-gnu-g++ not found"; exit 1; }
	@aarch64-linux-gnu-g++ $(CXXFLAGS) -c $< -o $@

# --- Rust backends ----------------------------------------------------------

# game/image/json/lua/network/server: self-contained cdylibs exporting
# lynxer_module_init_v1 and their ops, copied to stdlib/<name>.so for the
# interpreter to dlopen.
$(RUST_TARGET_DIR)/release/libclynxer_%.so: $(RUST_SOURCES)
	@command -v cargo >/dev/null || { echo "clynxer: cargo not found in PATH"; exit 1; }
	RUSTFLAGS="-C relocation-model=pic" cargo build -p clynxer_$* --release \
	    --manifest-path $(RUST_MANIFEST) --target-dir $(RUST_TARGET_DIR)

define RUST_MODULE_RULE
stdlib/$(1).so: $(RUST_TARGET_DIR)/release/libclynxer_$(1).so
	cp $$< $$@
endef
$(foreach name,$(RUST_MODULE_NAMES),$(eval $(call RUST_MODULE_RULE,$(name))))

$(NATIVE_SIGNATURE_MODULE): $(NATIVE_SIGNATURE_SOURCE)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $< -o $@

test: $(TARGET) $(NATIVE_BUILT) $(NATIVE_SIGNATURE_MODULE)