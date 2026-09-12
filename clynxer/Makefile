CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

TARGET := clynxer
SOURCE := main.cpp
CONDITION_FIXTURE := examples/conditions.lynx
LOOP_FIXTURE := examples/loops.lynx
ERROR_FIXTURE := examples/control_flow_error.lynx
COMMENT_FIXTURE := examples/comments.lynx
SETUP_ERROR_FIXTURE := examples/missing_setup.lynx

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $(TARGET)

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
	@echo "clynxer smoke test passed"

clean:
	rm -f $(TARGET)