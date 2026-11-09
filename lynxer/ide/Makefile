# Makefile for the Lynxer IDE - a native Rust GUI (iced), no webview.
#
# Run `make` / `make help` to list targets, and `make where` to print the
# absolute path of every produced executable.
#
# RUNTIME DEPENDENCY: the IDE shells out to the `lynxer` interpreter for
# Run / Lint / Format and validates it at startup. Lynxer is NOT needed to
# build the IDE, but it IS required to use it. `make check-lynxer` verifies
# it and `make lynxer` builds it from the repository root.

.DEFAULT_GOAL := help

TARGET_DIR  := target
DEBUG_BIN   := $(TARGET_DIR)/debug/lynxer-ide
RELEASE_BIN := $(TARGET_DIR)/release/lynxer-ide

REPO_ROOT  := ../..
LYNXER_EXE := $(if $(filter Windows_NT,$(OS)),lynxer.exe,lynxer)
LYNXER_BIN := $(REPO_ROOT)/lynxer/$(LYNXER_EXE)

.PHONY: help all build release run check clean where check-lynxer lynxer deps

help:
	@echo "Lynxer IDE (native Rust / iced) - make targets"
	@echo ""
	@echo "  make build / all   Build the debug binary"
	@echo "  make release       Build the optimized release binary"
	@echo "  make run           Verify deps, build, then launch the debug binary"
	@echo "  make check         Type-check with cargo check"
	@echo "  make clean         Remove target/ (cargo clean)"
	@echo "  make deps          Verify all runtime dependencies"
	@echo "  make check-lynxer  Verify the Lynxer interpreter is usable"
	@echo "  make lynxer        Build the Lynxer interpreter from the repo root"
	@echo "  make where         Print absolute paths of outputs + Lynxer"
	@echo "  make help          Show this help (default for a bare 'make')"
	@echo ""
	@echo "Depends on: the 'lynxer' interpreter (runtime, required)"
	@echo "  $(abspath $(LYNXER_BIN))"
	@echo "  Build it with 'make lynxer', put 'lynxer' on PATH, or set it in the IDE."
	@echo ""
	@echo "Where the executable is:"
	@echo "  Debug   : $(abspath $(DEBUG_BIN))"
	@echo "  Release : $(abspath $(RELEASE_BIN))"

all: build

# Hard runtime dependency: the IDE cannot run code without the interpreter.
check-lynxer:
	@if [ -f "$(LYNXER_BIN)" ]; then \
		bin="$(LYNXER_BIN)"; \
	elif command -v lynxer >/dev/null 2>&1; then \
		bin="$$(command -v lynxer)"; \
	else \
		echo "error: Lynxer interpreter not found at $(abspath $(LYNXER_BIN))"; \
		echo "       build it with 'make lynxer' (or 'make build' in the repo root),"; \
		echo "       or pick the executable with the IDE's 'Set Lynxer' button."; \
		exit 1; \
	fi; \
	"$$bin" --version >/dev/null 2>&1 || { \
		echo "error: '$$bin' exists but 'lynxer --version' failed."; \
		echo "       point the IDE at a working interpreter with 'Set Lynxer'."; \
		exit 1; \
	}; \
	echo "OK: Lynxer found ($$bin)"

lynxer:
	$(MAKE) -C $(REPO_ROOT) build
	@echo "OK: Lynxer built at $(abspath $(LYNXER_BIN))"

deps: check-lynxer
	@echo "OK: all runtime dependencies present."

build:
	cargo build
	@echo "OK: Lynxer IDE built (debug)."
	@echo "  Executable: $(abspath $(DEBUG_BIN))"
	@echo "  Launch it with: $(DEBUG_BIN)   (or: make run)"

release:
	cargo build --release
	@echo "OK: Lynxer IDE built (release)."
	@echo "  Executable: $(abspath $(RELEASE_BIN))"

run: build check-lynxer
	$(DEBUG_BIN)

check:
	cargo check

clean:
	cargo clean
	@echo "OK: Cleaned Lynxer IDE build artifacts."

where:
	@echo "Lynxer IDE build outputs (absolute paths):"
	@echo "  Debug binary   : $(abspath $(DEBUG_BIN))"
	@echo "  Release binary : $(abspath $(RELEASE_BIN))"
	@echo "  Lynxer (dep)   : $(abspath $(LYNXER_BIN))"
