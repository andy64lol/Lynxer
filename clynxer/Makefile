CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -fPIE

TARGET := clynxer
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

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(TARGET)
	@output="$$(./$(TARGET) examples/hello.lynx)"; \
	expected='Hello, Clynxer!'; \
	if [ "$$output" != "$$expected" ]; then \
		echo "expected: $$expected"; \
		echo "received: $$output"; \
		exit 1; \
	fi
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
	./$(TARGET) examples/$$fixture.lynx > .clynxer_direct.out 2>.clynxer_direct.err; \
	direct_status=$$?; \
	./$(TARGET) --compile --no-cache examples/$$fixture.lynx > /dev/null; \
	./$(TARGET) examples/$$fixture.lynxc > .clynxer_vm.out 2>.clynxer_vm.err; \
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
	@./$(TARGET) --bundle examples/hello.lynx .clynxer_bundled > /dev/null; 	bundled_output="$$(./.clynxer_bundled)"; 	if [ "$$bundled_output" != "Hello, Clynxer!" ]; then 	echo "bundled executable output mismatch: $$bundled_output"; exit 1; 	fi; 	rm -f .clynxer_bundled
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
	@echo "clynxer smoke test passed"

clean:
	rm -f $(TARGET) $(OBJECTS)