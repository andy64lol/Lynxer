# Lynxer Makefile
#   make install    — install deps + lynxer to /usr/local/bin  (may need sudo)
#   make uninstall  — remove the installed command and data
#   make test       — run the test suite
#   make clean      — remove Python byte-code caches
#   make help       — show this message
#   Set PREFIX=~/.local to install to ~/.local/bin (no sudo needed).

PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin
DATADIR  = $(PREFIX)/share/lynxer

PYTHON  ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)
SRC_DIR  = lynxer

.PHONY: install uninstall deps pip-install test clean help

# ── deps ─────────────────────────────────────────────────────────────────────
deps:
	@echo "Installing Python dependencies …"
	@$(PYTHON) -m pip install --quiet --upgrade cython setuptools
	@echo "✓  Dependencies installed."

# ── install ──────────────────────────────────────────────────────────────────
install: deps
	@echo "Installing Lynxer to $(BINDIR)/lynxer …"
	@mkdir -p "$(BINDIR)" "$(DATADIR)"
	@rm -rf "$(DATADIR)/lynxer"
	@cp -r "$(SRC_DIR)" "$(DATADIR)/lynxer"
	@printf '#!/usr/bin/env bash\nLYNXER_HOME="%s" exec $(PYTHON) "%s/lynxer/__main__.py" "$$@"\n' \
		"$(DATADIR)" "$(DATADIR)" > "$(BINDIR)/lynxer"
	@chmod +x "$(BINDIR)/lynxer"
	@echo "✓  Lynxer installed.  Try:  lynxer --version"

# ── uninstall ────────────────────────────────────────────────────────────────
uninstall:
	@echo "Removing Lynxer …"
	@rm -f "$(BINDIR)/lynxer"
	@rm -rf "$(DATADIR)"
	@echo "✓  Lynxer uninstalled."

# ── pip (editable) install ───────────────────────────────────────────────────
pip-install:
	$(PYTHON) -m pip install -e .

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
	@echo "  make install               Install to $(BINDIR) (may need sudo)"
	@echo "  make install PREFIX=~/.local  Install to ~/.local/bin (no sudo)"
	@echo "  make uninstall             Remove installed files"
	@echo "  make deps                  Install Python dependencies only"
	@echo "  make pip-install           Editable pip install (for development)"
	@echo "  make test                  Run the test suite"
	@echo "  make clean                 Remove byte-code caches"
