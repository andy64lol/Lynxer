# Lynxer Makefile
#   make test       — run the test suite
#   make clean      — remove Python byte-code caches
#   make help       — show this message

PYTHON  ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)

.PHONY: build test clean help

# ── build ────────────────────────────────────────────────────────────────────
build:
	@echo "Building Lynxer..."
	@$(PYTHON) -m PyInstaller \
		--onefile \
		--clean \
		--name lynxer \
		--add-data "lynxer/stdlib:stdlib" \
		 lynxer/shell.py
	@echo "✓  Build complete: dist/lynxer"

# ── test ─────────────────────────────────────────────────────────────────────
test:
	@echo "Running Lynxer test suite …"
	@cd lynxer && $(PYTHON) shell.py tests/test.lynx && \
	             $(PYTHON) shell.py tests/import_test.lynx && \
	             $(PYTHON) shell.py tests/rawPy_test.lynx && \
	             $(PYTHON) shell.py tests/newfeatures_test.lynx && \
	 echo "✓  All tests passed."

# ── clean ────────────────────────────────────────────────────────────────────
clean:
	@find . -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null; true
	@find . -name '*.pyc' -delete 2>/dev/null; true
	@echo "✓  Cleaned."

# ── help ─────────────────────────────────────────────────────────────────────
help:
	@echo "Lynxer build targets:"
	@echo "  make test                  Run the test suite"
	@echo "  make clean                 Remove byte-code caches"
