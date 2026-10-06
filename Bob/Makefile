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
	@cd /tmp/bob-module && out="$$($(CURDIR)/$(TARGET) --init-module)"; \
	printf '%s\n' "$$out" | grep -q 'Created module.toml' || { echo "bob --init-module did not report module.toml"; exit 1; }; \
	printf '%s\n' "$$out" | grep -q 'Created src/main.lynx' || { echo "bob --init-module did not report src/main.lynx"; exit 1; }; \
	grep -q 'name = "bob-module"' module.toml || { echo "module manifest is missing the module name"; exit 1; }; \
	grep -q 'entry = "src/main.lynx"' module.toml || { echo "module manifest is missing its entry"; exit 1; }; \
	grep -q 'global setup' src/main.lynx || { echo "module source is missing global setup()"; exit 1; }
	@rm -rf /tmp/bob-module
	@rm -rf /tmp/bob-install && mkdir -p /tmp/bob-install
	@cd /tmp/bob-install && out="$$(BOB_PREFIX=prefix $(CURDIR)/$(TARGET) --install-exec)"; \
	printf '%s\n' "$$out" | grep -q 'Installed' || { echo "bob --install-exec did not report an install"; exit 1; }; \
	if test -x prefix/bin/bob; then installed=prefix/bin/bob; else installed=prefix/bin/bob.exe; fi; \
	test -x "$$installed" || { echo "bob --install-exec did not install an executable"; exit 1; }; \
	"$$installed" --ver > /dev/null || { echo "the installed bob does not run"; exit 1; }
	@cd /tmp/bob-install && BOB_PREFIX=prefix $(CURDIR)/$(TARGET) --install-exec bogus > /dev/null 2>&1 && { echo "bob --install-exec accepted an argument"; exit 1; }; \
	true
	@rm -rf /tmp/bob-install
	@rm -rf /tmp/bob-config && mkdir -p /tmp/bob-config
	@BOB_CONFIG_DIR=/tmp/bob-config $(CURDIR)/$(TARGET) config set rest-api http://localhost:3000 > /dev/null
	@BOB_CONFIG_DIR=/tmp/bob-config $(CURDIR)/$(TARGET) config set github-token ghp_secretvalue123 > /dev/null
	@grep -q '"rest_api": "http://localhost:3000"' /tmp/bob-config/config.json || { echo "config.json is missing rest_api"; exit 1; }; \
	grep -q '"github_token": "ghp_secretvalue123"' /tmp/bob-config/config.json || { echo "config.json is missing github_token"; exit 1; }
	@out="$$(BOB_CONFIG_DIR=/tmp/bob-config $(CURDIR)/$(TARGET) config show)"; \
	printf '%s\n' "$$out" | grep -q 'rest-api = http://localhost:3000' || { echo "config show is missing rest-api"; exit 1; }; \
	printf '%s\n' "$$out" | grep -q 'ghp_\*\*\*\*' || { echo "config show did not mask the token"; exit 1; }; \
	printf '%s\n' "$$out" | grep -q 'ghp_secretvalue123' && { echo "config show leaked the token"; exit 1; }; \
	true
	@BOB_CONFIG_DIR=/tmp/bob-config $(CURDIR)/$(TARGET) config unset github-token > /dev/null; \
	grep -q '"github_token"' /tmp/bob-config/config.json && { echo "config unset left github_token"; exit 1; }; \
	true
	@rm -rf /tmp/bob-config
	@echo "bob self-check passed"

clean:
	@rm -rf target
	@echo "✓ Cleaned Bob build artifacts."

.PHONY: build test clean
