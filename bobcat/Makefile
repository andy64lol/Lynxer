# Bobcat — standalone IDEs extracted from Lynxer.
#
# The originals (lynxer/shell.cpp --cli-ide and lynxer/ide/) are untouched.

.PHONY: all help build release check test clean where

all: help

help:
	@echo "Bobcat IDEs"
	@echo "  make build      debug build of both IDEs"
	@echo "  make release    optimized build of both IDEs"
	@echo "  make check      cargo check for both"
	@echo "  make test       run tests for both"
	@echo "  make clean      cargo clean for both"
	@echo "  make where      print executable paths"

build:
	$(MAKE) -C cli build
	$(MAKE) -C ide build

release:
	$(MAKE) -C cli release
	$(MAKE) -C ide release

check:
	$(MAKE) -C cli check
	$(MAKE) -C ide check

test:
	$(MAKE) -C cli test
	$(MAKE) -C ide test

clean:
	$(MAKE) -C cli clean
	$(MAKE) -C ide clean

where:
	@echo "CLI: $(abspath cli/target/debug/bobcat-cli)"
	@echo "GUI: $(abspath ide/target/debug/bobcat-ide)"
