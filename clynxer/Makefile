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
RUST_MODULE_NAMES := game image json lua network server sound sqldb tui
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

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Remaining C++ stdlib modules, compiled to stdlib/<name>.so.
stdlib/%.so: stdlib/%.cpp
	$(CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $< -o $@

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
	@test -n "$(PYTHON)" || { echo "clynxer: python3 is required for $(CONTRACT_CHECK)"; exit 1; }
	@$(PYTHON) $(CONTRACT_CHECK)
	@printf 'Clynxer\n' > .clynxer_stdin
	@output="$$(./$(TARGET) examples/hello.lynx < .clynxer_stdin)"; \
	case "$$output" in \
	*"Hello, Clynxer!") ;; \
	*) echo "expected greeting 'Hello, Clynxer!', received: $$output"; exit 1;; \
	esac
	@output="$$(./$(TARGET) $(CONDITION_FIXTURE))"; \
	expected="$$(printf 'if branch\nelse branch')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected condition output:"; \
	printf '%s\n' "$$expected"; \
	echo "received condition output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$(./$(TARGET) $(COMMENT_FIXTURE))"; \
	expected="$$(printf '3\n/// this is string content, not a comment')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected comment output:"; \
	printf '%s\n' "$$expected"; \
	echo "received comment output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$(./$(TARGET) $(LOOP_FIXTURE))"; \
	expected="$$(printf 'while 0\nwhile 2\nfor 0\nfor 1\ndoWhile 1\ndoWhile 3\niterate 0\niterate 1\nforever 2')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected loop output:"; \
	printf '%s\n' "$$expected"; \
	echo "received loop output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$(./$(TARGET) $(INPUTLN_FIXTURE) < .clynxer_stdin)"; \
	expected="$$(printf 'Hello, world!\nClynxer')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected inputln output:"; printf '%s\n' "$$expected"; \
	echo "received inputln output:"; printf '%s\n' "$$output"; exit 1; fi
	@set +e; \
	output="$$(./$(TARGET) $(SETUP_ERROR_FIXTURE) 2>&1)"; \
	status=$$?; \
	set -e; \
expected="clynxer: $(SETUP_ERROR_FIXTURE):3:2: program must define global setup()"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected setup enforcement: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@output="$$(./$(TARGET) $(MILESTONE4_FIXTURE))"; \
	expected="$$(printf "2\\n7\\n5\\n-3\\n-6\\n-8\\n-7\\n12\\n4\\n32\\n-3\\nfalse\\ntrue\\ntrue\\nelif\\ncase\\ncaught: Cannot convert 'not an integer' to int\\n\\033")"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone4 output:"; printf '%s\n' "$$expected"; \
	echo "received milestone4 output:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$(./$(TARGET) $(MILESTONE4_PATTERN_FIXTURE))"; \
	if [ "$$output" != "2" ]; then \
	echo "expected milestone4 pattern output: 2"; \
	echo "received: $$output"; exit 1; fi
	@set +e; \
	output="$$(./$(TARGET) $(ERROR_FIXTURE) 2>&1)"; \
	status=$$?; \
	set -e; \
expected="clynxer: $(ERROR_FIXTURE):4:8: unknown variable 'missing'"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected source-located error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@output="$$(./$(TARGET) examples/builtins.lynx)"; \
	expected="$$(printf '99 3.14 true\n42 3\n1.5\n[0, 1, 2, 3, 4]\n[2, 3, 4, 5, 6, 7]\n[0, 2, 4, 6, 8]\n[10, 9, 8, 7, 6, 5, 4, 3, 2, 1]\n[]\n[0, 2, 4, 6, 8]\n[5, 3, 9, 1, 7]\n7 [5, 3, 9, 1, 7]\n7\n[100, 3, 9, 1, 7]\n[3, 9]\ntrue false\n2 -1\n5-3-9-1-7\ntrue true false\n25 5\n[1, 3, 5, 7, 9] [9, 7, 5, 3, 1]\n[7, 1, 9, 3, 5]\n1 9\n[5, 3] [1, 7]\n5 7\n1\n[5, 3, 9, 1, 7, 8, 8]\n[5, 50, 3, 9, 1, 7]\n[1, 2, 3, 4]\n[8]\n[x, x, x]\n[{"a": 1, "b": "a"}, {"a": 2, "b": "b"}]\n[1, "two", true, null]\n{"name": "Ada", "age": 36}\n[a, b, c]\ntrue\n(10, 20) (1, two, 3)\n3 two 3\ntrue 0\n(10,)\n[10, 20] (4, 5)\n(10, 20, 1, two, 3)\n1\n1 3\n[10, 20]\n(20, 10)\n(1, 2, 3) (3, 2, 1)\n10 20 30\ntrue true\n(1, 2)\n3\n({"a": 1, "b": "a"}, {"a": 2, "b": "b"})\n10-20\nint float str\nbool none list\n5 5\nMISSING sentinel <sentinel>\n<object> object\n42\n-7\n200\n305419896\n4 8 2\nHello, Ada!\nAda is 36\n40\nno {name} interpolation here\n3')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected builtins output:"; \
	printf '%s\n' "$$expected"; \
	echo "received builtins output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@for fixture in hello conditions loops comments builtins milestone4 milestone5 milestone5_codeblocks; do \
	./$(TARGET) examples/$$fixture.lynx < .clynxer_stdin > .clynxer_direct.out 2>.clynxer_direct.err; \
	direct_status=$$?; \
	if ! ./$(TARGET) --compile examples/$$fixture.lynx .clynxer_compiled > /dev/null; then \
	echo "compile failed for $$fixture"; exit 1; fi; \
	./.clynxer_compiled < .clynxer_stdin > .clynxer_compiled.out 2>.clynxer_compiled.err; \
	compiled_status=$$?; \
	if ! diff -q .clynxer_direct.out .clynxer_compiled.out > /dev/null || \
	   ! diff -q .clynxer_direct.err .clynxer_compiled.err > /dev/null || \
	   [ "$$direct_status" -ne "$$compiled_status" ]; then \
	echo "compiled-executable parity failed for $$fixture"; \
	diff .clynxer_direct.out .clynxer_compiled.out | head -5; \
	exit 1; \
	fi; \
	done
	@for fixture in native_stdlibs milestone6_module milestone6_math_native stdlib_json stdlib_re stdlib_path; do \
	./$(TARGET) examples/$$fixture.lynx > .clynxer_direct.out 2>&1; \
	direct_status=$$?; \
	if ! ./$(TARGET) --compile examples/$$fixture.lynx .clynxer_compiled > /dev/null; then \
	echo "compile failed for $$fixture (imports)"; exit 1; fi; \
	./.clynxer_compiled > .clynxer_compiled.out 2>&1; \
	compiled_status=$$?; \
	if ! diff -q .clynxer_direct.out .clynxer_compiled.out > /dev/null || \
	   [ "$$direct_status" -ne "$$compiled_status" ]; then \
	echo "compiled import parity failed for $$fixture"; \
	diff .clynxer_direct.out .clynxer_compiled.out | head -5; \
	exit 1; \
	fi; \
	done
	@./$(TARGET) --bundle examples/hello.lynx .clynxer_bundled > /dev/null; \
	bundled_output="$$(./.clynxer_bundled < .clynxer_stdin)"; \
	case "$$bundled_output" in \
	*"Hello, Clynxer!") ;; \
	*) echo "bundled executable output mismatch: $$bundled_output"; exit 1;; \
	esac
	@./$(TARGET) --compile examples/bundle_app.lynx examples/bundle_extras/greeter.lynx examples/bundle_extras/counter.lynx -o .clynxer_multi > /dev/null; \
	multi_output="$$(./.clynxer_multi)"; \
	expected="$$(printf 'hello, LYNXER!\n1,2,3')"; \
	if [ "$$multi_output" != "$$expected" ]; then \
	echo "multi-file bundle output mismatch:"; printf '%s\n' "$$multi_output"; exit 1; fi
	@./$(TARGET) --compile examples/bundle_assets.lynx --include examples/bundle_extras/message.txt -o .clynxer_assets > /dev/null; \
	assets_output="$$(./.clynxer_assets)"; \
	expected="$$(printf '[message.txt]\nbundled payload')"; \
	if [ "$$assets_output" != "$$expected" ]; then \
	echo "included data file output mismatch:"; printf '%s\n' "$$assets_output"; exit 1; fi
	@printf 'CLYXC\x00\x01\x01not-real-bytecode' > .clynxer_bad.lynxc; \
	if ./$(TARGET) .clynxer_bad.lynxc > /dev/null 2>.clynxer_bad.err; then \
	echo "legacy bytecode file was accepted"; exit 1; \
	fi; \
	grep -q "no longer supported" .clynxer_bad.err || { echo "bytecode removal error missing"; exit 1; }; \
	rm -f .clynxer_bad.lynxc .clynxer_bad.err .clynxer_direct.out .clynxer_direct.err \
	.clynxer_compiled.out .clynxer_compiled.err .clynxer_compiled .clynxer_bundled \
	.clynxer_multi .clynxer_assets
	@output="$$(./$(TARGET) examples/milestone3.lynx)"; \
	expected="$$(printf '9\nAda\n90\n5\nEngineer\n6\n42\n1\n0\n255\n-12\n0.25\nA\n[1, 2, 3]\n(left, 2)')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone3 output:"; \
	printf '%s\n' "$$expected"; \
	echo "received milestone3 output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$(./$(TARGET) examples/m3_scalars.lynx)"; \
	expected="$$(printf '3.5\nZ\n1\n0\n255\n-128\n-300\n100000\n5000000000\n200\n60000\n3000000000\n9000000000\n1.5\n2.25\n[1, 2, 3]\n(10, 20)\n42\n9')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected m3_scalars output:"; \
	printf '%s\n' "$$expected"; \
	echo "received m3_scalars output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@output="$$(./$(TARGET) examples/m3_scope.lynx)"; \
	expected="$$(printf '1')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected m3_scope output (flat per-function scope):"; \
	printf '%s\n' "$$expected"; \
	echo "received m3_scope output:"; \
	printf '%s\n' "$$output"; \
	exit 1; \
	fi
	@set +e; \
	output="$$(./$(TARGET) examples/m3_errors.lynx 2>&1)"; \
	status=$$?; \
	set -e; \
	expected="clynxer: examples/m3_errors.lynx:4:5: value 200 is out of range for type 'int8'"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected out-of-range error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@set +e; \
	output="$$(./$(TARGET) examples/m3_const.lynx 2>&1)"; \
	status=$$?; \
	set -e; \
	expected="clynxer: examples/m3_const.lynx:5:5: variable 'frozen' is constant and cannot be reassigned"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected const reassignment error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@set +e; \
	output="$$(./$(TARGET) examples/m3_vargroup.lynx 2>&1)"; \
	status=$$?; \
	set -e; \
	expected="clynxer: examples/m3_vargroup.lynx:8:5: Vargroup and legacy class-field assignment requires an explicit type"; \
	if [ "$$status" -ne 1 ] || [ "$$output" != "$$expected" ]; then \
	echo "expected vargroup explicit-type error: $$expected"; \
	echo "received (status $$status): $$output"; \
	exit 1; \
	fi
	@output="$$(./$(TARGET) $(MILESTONE5_FIXTURE))"; \
	expected="$$(printf '5\n7\n25')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone5 output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$(./$(TARGET) $(MILESTONE5_CODEBLOCK_FIXTURE))"; \
	expected="$$(printf 'one\ntwo\nsaved\nafter')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone5 codeblock output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$(./$(TARGET) $(MILESTONE6_FIXTURE))"; \
	expected="$$(printf '6\n42\n6\n6\n7\n\033[31mok\033[0m\ntrue\ncba\n4')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected milestone6 output:"; printf '%s\n' "$$expected"; \
	echo "received:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$(./$(TARGET) $(MILESTONE6_MATH_FIXTURE))"; \
	expected="$$(printf '3\n0\ntrue\ntrue\ntrue is now a string.\n42 is a integer.\nint is the type of number.')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native math output:"; printf '%s\n' "$$expected"; \
	echo "received native math output:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$(./$(TARGET) $(NATIVE_STDLIB_FIXTURE))"; \
	expected="$$(printf '3.141592653589793\n180\ntrue\ntrue\ntrue\nlinux')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native stdlib output:"; printf '%s\n' "$$expected"; \
	echo "received native stdlib output:"; printf '%s\n' "$$output"; exit 1; fi
	@output="$$(./$(TARGET) $(NATIVE_SIGNATURE_FIXTURE))"; \
	expected="$$(printf '7\n2.5\nzero\n7\n9\ncopy\n4\n1\n---\n6\n1.25\n4\n6.5\n4\n3.5\n1.500000\n2.75\nabcd\n3.75\nabc\nab5\nabc5\n7\nn12\n3\n9')"; \
	if [ "$$output" != "$$expected" ]; then \
	echo "expected native signature output:"; printf '%s\n' "$$expected"; \
	echo "received native signature output:"; printf '%s\n' "$$output"; exit 1; fi
	@list_output="$$(cd /tmp && "$(CURDIR)/$(TARGET)" --list-stdlibs)"; \
	for module in $(LIST_STDLIB_MODULES); do \
	if ! printf '%s\n' "$$list_output" | grep -Fqx "  $$module"; then \
	echo "stdlib listing is missing module: $$module"; exit 1; fi; \
	done
	@for fixture in $(STDLIB_FIXTURES); do \
	expected="$${fixture%.lynx}.expected"; \
	if [ ! -f "$$expected" ]; then \
	echo "missing expected output for $$fixture"; exit 1; fi; \
	CLYNXER_GAME_HEADLESS=1 ./$(TARGET) "$$fixture" > .clynxer_stdlib.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "stdlib fixture failed: $$fixture"; cat .clynxer_stdlib.out; \
	rm -f .clynxer_stdlib.out; exit 1; fi; \
	if ! diff -u "$$expected" .clynxer_stdlib.out > .clynxer_stdlib.diff; then \
	echo "stdlib fixture output mismatch: $$fixture"; \
	cat .clynxer_stdlib.diff; \
	rm -f .clynxer_stdlib.out .clynxer_stdlib.diff; exit 1; fi; \
	done; \
	rm -f .clynxer_stdlib.out .clynxer_stdlib.diff
	@CLYNXER_GAME_HEADLESS=1 ./$(TARGET) $(STDLIB_TEST_ALL) > .clynxer_stdlib_all.out 2>&1; \
	if [ $$? -ne 0 ]; then \
	echo "consolidated stdlib test failed: $(STDLIB_TEST_ALL)"; \
	cat .clynxer_stdlib_all.out; \
	rm -f .clynxer_stdlib_all.out; exit 1; fi; \
	rm -f .clynxer_stdlib_all.out
	@CLYNXER_GAME_HEADLESS=1 ./$(TARGET) examples/game_clicker.lynx >/dev/null 2>&1 || \
	{ echo "example failed: examples/game_clicker.lynx"; exit 1; }
	@rm -f .clynxer_stdin
	@echo "clynxer smoke test passed"

clean:
	rm -f $(TARGET) $(OBJECTS) $(NATIVE_MODULES) $(NATIVE_SIGNATURE_MODULE) .clynxer_stdin
	rm -f .clynxer_stdlib_all.out
	rm -rf build $(RUST_DIR)/target $(RUST_DIR)/*/target
