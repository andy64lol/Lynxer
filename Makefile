# Lynxer Makefile
#   make build      — create venv, install deps, then build with PyInstaller
#   make clean      — remove Python byte-code caches and build artefacts
#   make help       — show this message

PYTHON   ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)
VENV     := venv
VENV_PY  := $(VENV)/bin/python
VENV_PIP := $(VENV)/bin/pip

.PHONY: build clean help

# ── build ────────────────────────────────────────────────────────────────────
build:
	@if [ ! -d "$(VENV)" ]; then \
		echo "Creating virtual environment '$(VENV)'..."; \
		$(PYTHON) -m venv $(VENV); \
	else \
		echo "Using existing virtual environment '$(VENV)'."; \
	fi

	@echo "Upgrading pip..."
	@$(VENV_PIP) install --upgrade pip

	@echo "Synchronizing dependencies with requirements_venv.txt..."
	@$(VENV_PIP) install --upgrade -r requirements_venv.txt

	@echo "Ensuring PyInstaller is installed..."
	@$(VENV_PIP) install --upgrade pyinstaller

	@echo "Building Lynxer..."
	@$(VENV)/bin/pyinstaller \
		--onefile \
		--clean \
		--name lynxer \
		--add-data "lynxer/stdlib:stdlib" \
		lynxer/shell.py

	@echo "✓ Build complete: dist/lynxer"

# ── clean ────────────────────────────────────────────────────────────────────
clean:
	@find . -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null; true
	@find . -name '*.pyc' -delete 2>/dev/null; true
	@find . -name '*.lynxc' -delete 2>/dev/null; true
	@rm -rf build/ dist/ lynxer.spec 2>/dev/null; true
	@echo "✓  Cleaned."

# ── help ─────────────────────────────────────────────────────────────────────
help:
	@echo "Lynxer build targets:"
	@echo "  make build                 Create venv, install deps, build binary with PyInstaller"
	@echo "  make clean                 Remove byte-code caches, .lynxc files, and build artefacts"
