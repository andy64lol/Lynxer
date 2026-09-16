CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -fPIE

TARGET := clynxer
NATIVE_SOURCES := $(wildcard stdlib/*.cpp)
NATIVE_MODULES := $(NATIVE_SOURCES:.cpp=.so)
include stdlib/libs.mk

# Native modules that need an opt-in system library. When the package cannot be
# resolved by pkg-config the module is skipped with a warning, so a plain build
# never fails because an optional dependency is absent.
OPTIONAL_MODULE_NAMES := http lua tui image game
OPTIONAL_MODULE_PATHS := $(OPTIONAL_MODULE_NAMES:%=stdlib/%.so)
OPTIONAL_NATIVE_MODULES := $(filter $(NATIVE_MODULES),$(OPTIONAL_MODULE_PATHS))
$(foreach path,$(OPTIONAL_NATIVE_MODULES),$(eval MODULE_NAME_$(path) := $(patsubst stdlib/%.so,%,$(path))))
$(foreach path,$(OPTIONAL_NATIVE_MODULES),$(eval HAVE_$(path) := $(shell pkg-config --exists $(MODULE_PKG_$(MODULE_NAME_$(path))) 2>/dev/null && echo 1)))
OPTIONAL_MODULES_PRESENT := $(foreach path,$(OPTIONAL_NATIVE_MODULES),$(if $(HAVE_$(path)),$(path)))
OPTIONAL_MODULES_SKIPPED := $(filter-out $(OPTIONAL_MODULES_PRESENT),$(OPTIONAL_NATIVE_MODULES))
CORE_MODULES := $(filter-out $(OPTIONAL_MODULE_PATHS),$(NATIVE_MODULES))
NATIVE_BUILT := $(CORE_MODULES) $(OPTIONAL_MODULES_PRESENT)
$(foreach path,$(OPTIONAL_MODULES_SKIPPED),$(warning clynxer: skipping $(path): pkg-config package '$(MODULE_PKG_$(MODULE_NAME_$(path)))' not found))

SOURCES := main.cpp shell.cpp lexer.cpp runtime.cpp types.cpp builtins.cpp ops.cpp ast.cpp parser.cpp config.cpp compiler.cpp vm.cpp bytecode.cpp bundle.cpp
HEADERS := $(wildcard *.hpp)
OBJECTS := $(SOURCES:.cpp=.o)
CONDITION_FIXTURE := examples/conditions.lynx
LOOP_FIXTURE := examples/loops.lynx
ERROR_FIXTURE := examples/control_flow_error.lynx
COMMENT_FIXTURE := examples/comments.lynx
SETUP_ERROR_FIXTURE := examples/missing_setup.lynx
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

.PHONY: all clean test

all: $(TARGET) $(NATIVE_BUILT)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

stdlib/%.so: stdlib/%.cpp
	$(eval _pkg := $(MODULE_PKG_$*))
	$(eval _cflags := $(if $(strip $(_pkg)),$(shell pkg-config --cflags $(_pkg) 2>/dev/null)))
	$(eval _libs := $(if $(strip $(_pkg)),$(shell pkg-config --libs $(_pkg) 2>/dev/null)))
	$(CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $(_cflags) $< -o $@ $(_libs) $(MODULE_FLAGS_$*)

$(NATIVE_SIGNATURE_MODULE): $(NATIVE_SIGNATURE_SOURCE)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -pedantic -fPIC -shared $< -o $@

test: $(TARGET) $(NATIVE_BUILT) $(NATIVE_SIGNATURE_MODULE)
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
	@for fixture in hello conditions loops comments builtins milestone4; do \
	./$(TARGET) examples/$$fixture.lynx < .clynxer_stdin > .clynxer_direct.out 2>.clynxer_direct.err; \
	direct_status=$$?; \
	./$(TARGET) --compile --no-cache examples/$$fixture.lynx > /dev/null; \
	./$(TARGET) examples/$$fixture.lynxc < .clynxer_stdin > .clynxer_vm.out 2>.clynxer_vm.err; \
	vm_status=$$?; \
	if ! diff -q .clynxer_direct.out .clynxer_vm.out > /dev/null || \
	   ! diff -q .clynxer_direct.err .clynxer_vm.err > /dev/null || \
	   [ "$$direct_status" -ne "$$vm_status" ]; then \
	echo "bytecode parity failed for $$fixture"; \
	diff .clynxer_direct.out .clynxer_vm.out | head -5; \
	exit 1; \
	fi; \
	done
	@./$(TARGET) --compile examples/hello.lynx | grep -q "bytecode is up to date" || { echo "bytecode cache check failed"; exit 1; }
	@./$(TARGET) --view-bytecode examples/hello.lynxc | grep -q "PUSH_CONST" || { echo "view-bytecode smoke failed"; exit 1; }
	@./$(TARGET) --view-bytecode examples/hello.lynxc | grep -q "HALT" || { echo "view-bytecode HALT smoke failed"; exit 1; }
	@./$(TARGET) --bundle examples/hello.lynx .clynxer_bundled > /dev/null; 	bundled_output="$$(./.clynxer_bundled < .clynxer_stdin)"; 	case "$$bundled_output" in 	*"Hello, Clynxer!") ;; 	*) echo "bundled executable output mismatch: $$bundled_output"; exit 1;; 	esac; 	rm -f .clynxer_bundled
	@printf 'CLYXC\x00\x01\x01not-real-bytecode' > .clynxer_bad.lynxc; \
	if ./$(TARGET) .clynxer_bad.lynxc > /dev/null 2>.clynxer_bad.err; then \
	echo "corrupt bytecode was accepted"; exit 1; \
	fi; \
	grep -q "invalid bytecode" .clynxer_bad.err || { echo "corrupt bytecode error missing"; exit 1; }; \
	rm -f .clynxer_bad.lynxc .clynxer_bad.err .clynxer_direct.out .clynxer_direct.err .clynxer_vm.out .clynxer_vm.err
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
	@for fixture in $(MILESTONE5_FIXTURE:.lynx=) $(MILESTONE5_CODEBLOCK_FIXTURE:.lynx=); do \
	./$(TARGET) --compile --no-cache $$fixture.lynx > /dev/null; \
	./$(TARGET) $$fixture.lynxc > .clynxer_vm.out 2>.clynxer_vm.err; \
	./$(TARGET) $$fixture.lynx > .clynxer_direct.out 2>.clynxer_direct.err; \
	if ! diff -q .clynxer_direct.out .clynxer_vm.out > /dev/null || \
	   ! diff -q .clynxer_direct.err .clynxer_vm.err > /dev/null; then \
	echo "bytecode parity failed for $$fixture"; exit 1; fi; \
	done; \
	rm -f .clynxer_vm.out .clynxer_vm.err .clynxer_direct.out .clynxer_direct.err \
	$(MILESTONE5_FIXTURE:.lynx=.lynxc) $(MILESTONE5_CODEBLOCK_FIXTURE:.lynx=.lynxc)
	@for fixture in $(STDLIB_FIXTURES); do \
	expected="$${fixture%.lynx}.expected"; \
	if [ ! -f "$$expected" ]; then \
	echo "missing expected output for $$fixture"; exit 1; fi; \
	if ! ./$(TARGET) "$$fixture" > .clynxer_stdlib.out 2>&1; then \
	echo "stdlib fixture failed: $$fixture"; cat .clynxer_stdlib.out; \
	rm -f .clynxer_stdlib.out; exit 1; fi; \
	if ! diff -u "$$expected" .clynxer_stdlib.out > .clynxer_stdlib.diff; then \
	echo "stdlib fixture output mismatch: $$fixture"; \
	cat .clynxer_stdlib.diff; \
	rm -f .clynxer_stdlib.out .clynxer_stdlib.diff; exit 1; fi; \
	done; \
	rm -f .clynxer_stdlib.out .clynxer_stdlib.diff
	@rm -f .clynxer_stdin
	@echo "clynxer smoke test passed"

clean:
	rm -f $(TARGET) $(OBJECTS) $(NATIVE_MODULES) $(NATIVE_SIGNATURE_MODULE) .clynxer_stdin