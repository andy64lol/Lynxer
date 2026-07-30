PYTHON   ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)
VENV     := venv
VENV_PY  := $(VENV)/bin/python
VENV_PIP := $(VENV)/bin/pip
PYINSTALLER := $(VENV)/bin/pyinstaller

COLLECT_ALL := $(shell \
	sed 's/#.*//' requirements_venv.txt | \
	sed '/^[[:space:]]*$$/d' | \
	sed 's/[<>=!~].*//' | \
	sed 's/\[.*\]//' | \
	xargs -I{} printf -- "--collect-all=%s " "{}" \
)

.PHONY: build clean help

build:
	@if [ ! -d "$(VENV)" ]; then \
		echo "Creating virtual environment '$(VENV)'..."; \
		$(PYTHON) -m venv $(VENV); \
	else \
		echo "Using existing virtual environment '$(VENV)'."; \
	fi

	@echo "Upgrading pip..."
	@$(VENV_PIP) install --upgrade pip

	@echo "Installing dependencies..."
	@$(VENV_PIP) install --upgrade -r requirements_venv.txt

	@echo "Installing PyInstaller..."
	@$(VENV_PIP) install --upgrade pyinstaller

	@echo "Building Lynxer..."
	@$(PYINSTALLER) \
		--onefile \
		--clean \
		$(COLLECT_ALL) \
		--name lynxer \
		--add-data "lynxer/stdlib:stdlib" \
		lynxer/shell.py

	@echo "✓ Build complete: dist/lynxer"

clean:
	@find . -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null || true
	@find . -name '*.pyc' -delete 2>/dev/null || true
	@find . -name '*.lynxc' -delete 2>/dev/null || true
	@rm -rf build dist *.spec 2>/dev/null || true
	@echo "✓ Cleaned."

help:
	@echo "Lynxer build targets:"
	@echo "  make build"
	@echo "  make clean"