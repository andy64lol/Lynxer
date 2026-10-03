# Bob — the Lynxer package manager. A standalone Rust project, separate from
# Lynxer; it is not built by Lynxer's Makefile.
CARGO ?= cargo
TARGET := target/release/bob

# On Windows the MinGW toolchain links its runtime (libgcc/libwinpthread)
# dynamically by default, which would leave bob.exe needing MSYS2 DLLs on PATH.
# Build a static CRT so the Windows binary is standalone and runs from any
# shell — including cmd.exe and PowerShell, not just MSYS2.
ifeq ($(OS),Windows_NT)
BOB_RUSTFLAGS ?= -C target-feature=+crt-static
else
BOB_RUSTFLAGS ?=
endif

build: $(TARGET)
	@echo "✓ Bob build complete: $(TARGET)"

$(TARGET): Cargo.toml $(wildcard src/*.rs)
	RUSTFLAGS="$(BOB_RUSTFLAGS)" $(CARGO) build --release

# Scaffolds both project kinds and checks what was written, not just that the
# files exist. Runs under a POSIX shell (MSYS2 or Git Bash on Windows), which
# the workflows provide.
test: $(TARGET)
	@$(TARGET) --ver > /dev/null
	@rm -rf /tmp/bob-selfcheck && mkdir -p /tmp/bob-selfcheck
	@cd /tmp/bob-selfcheck && out="$$($(CURDIR)/$(TARGET) --init)"; \
	printf '%s\n' "$$out" | grep -q 'Created bob/bob.toml' || { echo "bob --init did not report bob/bob.toml"; exit 1; }; \
	printf '%s\n' "$$out" | grep -q 'Created bob/bob-lock.toml' || { echo "bob --init did not report bob/bob-lock.toml"; exit 1; }; \
	grep -q '^\[package\]' bob/bob.toml || { echo "manifest is missing the [package] table"; exit 1; }; \
	grep -q 'name = "bob-selfcheck"' bob/bob.toml || { echo "manifest is missing the project name"; exit 1; }; \
	grep -q '^\[dependencies\]' bob/bob.toml || { echo "manifest is missing the [dependencies] table"; exit 1; }; \
	grep -q '^version = 1' bob/bob-lock.toml || { echo "lock file is missing its version"; exit 1; }; \
	test -d bob/packages || { echo "bob --init did not create bob/packages"; exit 1; }
	@rm -rf /tmp/bob-selfcheck
	@rm -rf /tmp/bob-module && mkdir -p /tmp/bob-module
	@cd /tmp/bob-module && out="$$($(CURDIR)/$(TARGET) --init --module)"; \
	printf '%s\n' "$$out" | grep -q 'Created module.toml' || { echo "bob --init --module did not report module.toml"; exit 1; }; \
	printf '%s\n' "$$out" | grep -q 'Created src/main.lynx' || { echo "bob --init --module did not report src/main.lynx"; exit 1; }; \
	grep -q 'name = "bob-module"' module.toml || { echo "module manifest is missing the module name"; exit 1; }; \
	grep -q 'entry = "src/main.lynx"' module.toml || { echo "module manifest is missing its entry"; exit 1; }; \
	grep -q 'global setup' src/main.lynx || { echo "module source is missing global setup()"; exit 1; }
	@rm -rf /tmp/bob-module
	@echo "bob self-check passed"

clean:
	@rm -rf target
	@echo "✓ Cleaned Bob build artifacts."

.PHONY: build test clean
