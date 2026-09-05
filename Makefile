PYTHON   ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)
VENV     := venv
VENV_PY  := $(VENV)/bin/python
VENV_PIP := $(VENV_PY) -m pip
PYINSTALLER := $(VENV)/bin/pyinstaller

WARNING_FILE := lynxer/warnings.txt
WARNING_DATA := --add-data "$(WARNING_FILE):lynxer"

COLLECT_ALL := $(shell \
	sed 's/#.*//' requirements_venv.txt | \
	sed '/^[[:space:]]*$$/d' | \
	sed 's/[<>=!~].*//' | \
	sed 's/\[.*\]//' | \
	xargs -I{} printf -- "--collect-all=%s " "{}" \
)

CYTHON_COLLECT_ALL := --collect-all=Cython --collect-all=setuptools

SYSTEM_CALLS_DEP := system-calls
SYSTEM_CALLS := --hidden-import system_calls --hidden-import lynxer.syscalls --collect-submodules system_calls --collect-all=system_calls

.PHONY: venv platform-check build buildLite buildCpp test validate check clean help

venv:
	@if [ ! -d "$(VENV)" ]; then \
		echo "Creating virtual environment '$(VENV)'..."; \
		$(PYTHON) -m venv $(VENV); \
	else \
		echo "Using existing virtual environment '$(VENV)'."; \
	fi
	@echo "Upgrading pip and setuptools..."
	@$(VENV_PIP) install --upgrade pip setuptools

test: buildCpp
	@echo "Running tests..."
	@$(VENV_PY) -u test/validate.py

validate: buildCpp
	@echo "Running validation..."
	@$(VENV_PY) -u lynxer/validate.py

check: test
	@for file in syntax.lynx test/*.lynx; do \
		$(VENV_PY) lynxer/shell.py --lint "$$file" >/dev/null || exit $$?; \
	done
	@echo "✓ Lynxer checks passed."

platform-check: venv
	@echo "Checking Linux build platform..."
	@$(VENV_PY) -c 'from lynxer.syscalls import require_supported_platform, WORD_BYTES; architecture = require_supported_platform(); print(f"  -> {architecture} ({WORD_BYTES * 8}-bit Python ABI)")'

build: platform-check buildCpp
	@echo "Installing dependencies..."
	@$(VENV_PIP) install --upgrade -r requirements_venv.txt

	@echo "Installing the Linux syscall tables..."
	@$(VENV_PIP) install --upgrade $(SYSTEM_CALLS_DEP)

	@echo "Patching Arcade PyInstaller hook... (due to a bug)"
	@HOOK=$$($(VENV_PY) -c 'import arcade, os; print(os.path.join(os.path.dirname(arcade.__file__), "__pyinstaller", "hook-arcade.py"))'); \
	if [ -f "$$HOOK" ]; then \
		sed -i.bak 's|"./arcade/VERSION"|"./arcade"|g' "$$HOOK"; \
		rm -f "$$HOOK.bak"; \
		echo "  -> Patched $$HOOK"; \
	else \
		echo "  -> Hook file not found (Arcade may not be installed?)"; \
	fi

	@echo "Installing PyInstaller..."
	@$(VENV_PIP) install --upgrade pyinstaller

	@echo "Building Lynxer..."
	@$(PYINSTALLER) \
		--onefile \
		--clean \
		$(COLLECT_ALL) \
		--name lynxer \
		--hidden-import lynxer.cpp \
		$(SYSTEM_CALLS) \
		$(WARNING_DATA) \
		--add-data "lynxer/stdlib:stdlib" \
		lynxer/shell.py

	@echo "✓ Build complete: dist/lynxer"

buildLite: platform-check buildCpp
	@echo "Installing PyInstaller and Cython runtime dependencies..."
	@$(VENV_PIP) install --upgrade pyinstaller cython setuptools

	@echo "Installing the Linux syscall tables..."
	@$(VENV_PIP) install --upgrade $(SYSTEM_CALLS_DEP)

	@echo "Selecting pure stdlib .lynx modules..."
	@rm -rf build/stdlib_pure || true
	@$(VENV_PY) scripts/select_pure_stdlib.py lynxer/stdlib build/stdlib_pure

	@echo "Building Lynxer (lite) with Cython support and only pure stdlib modules..."
	@$(PYINSTALLER) \
		--onefile \
		--clean \
		$(CYTHON_COLLECT_ALL) \
		--hidden-import Cython.Build.Inline \
		--name lynxer-lite \
		--hidden-import lynxer.cpp \
		$(SYSTEM_CALLS) \
		$(WARNING_DATA) \
		--add-data "build/stdlib_pure:stdlib" \
		lynxer/shell.py

	@echo "✓ Lite build complete: dist/lynxer-lite"

buildCpp: venv
	@echo "Building Lynxer C++ memory extension..."
	@$(VENV_PY) lynxer/setup.py build_ext --inplace
	@echo "✓ C++ extension built in lynxer/"

clean:
	@find . -name '__pycache__' -type d -exec rm -rf {} + 2>/dev/null || true
	@find . -name '*.pyc' -delete 2>/dev/null || true
	@find . -name '*.lynxc' -delete 2>/dev/null || true
	@rm -rf build dist *.spec lynxer/build lynxer/*.so 2>/dev/null || true
	@echo "✓ Cleaned."

help:
	@echo "Lynxer build targets:"
	@echo "  make build"
	@echo "  make buildLite"
	@echo "  make buildCpp"
	@echo "  make platform-check"
	@echo "  make venv"
	@echo "  make test"
	@echo "  make check"
	@echo "  make clean"
	@echo "  make help"
	@echo ""
	@echo "Lynxer source commands:"
	@echo "  lynxer --format <file.lynx>"
	@echo "  lynxer --format-oneline <file.lynx>"
	@echo "  lynxer --ast <file.lynx>"
	@echo "  lynxer --lint <file.lynx>"
