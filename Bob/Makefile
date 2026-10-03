# Bob — the Lynxer package manager. A standalone Rust project, separate from
# Lynxer; it is not built by Lynxer's Makefile.
CARGO ?= cargo
TARGET := target/release/bob

build: $(TARGET)
	@echo "✓ Bob build complete: $(TARGET)"

$(TARGET): Cargo.toml $(wildcard src/*.rs)
	$(CARGO) build --release

test: $(TARGET)
	@$(TARGET) --ver > /dev/null
	@rm -rf /tmp/bob-selfcheck && mkdir -p /tmp/bob-selfcheck
	@cd /tmp/bob-selfcheck && $(CURDIR)/$(TARGET) --init > /dev/null
	@test -f /tmp/bob-selfcheck/bob/bob.toml || { echo "bob --init did not write bob/bob.toml"; exit 1; }
	@test -f /tmp/bob-selfcheck/bob/bob-lock.toml || { echo "bob --init did not write bob/bob-lock.toml"; exit 1; }
	@rm -rf /tmp/bob-selfcheck
	@rm -rf /tmp/bob-module && mkdir -p /tmp/bob-module
	@cd /tmp/bob-module && $(CURDIR)/$(TARGET) --init --module > /dev/null
	@test -f /tmp/bob-module/module.toml || { echo "bob --init --module did not write module.toml"; exit 1; }
	@test -f /tmp/bob-module/src/main.lynx || { echo "bob --init --module did not write src/main.lynx"; exit 1; }
	@rm -rf /tmp/bob-module
	@echo "bob self-check passed"

clean:
	@rm -rf target
	@echo "✓ Cleaned Bob build artifacts."

.PHONY: build test clean
