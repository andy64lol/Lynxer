.PHONY: all help build release check test clean run where

all: help

help:
	@echo "Bobcat CLI IDE"
	@echo "  make build     debug build"
	@echo "  make release   optimized build"
	@echo "  make check     cargo check"
	@echo "  make test      run tests"
	@echo "  make run FILE=file.lynx   build + launch"
	@echo "  make clean     cargo clean"
	@echo "  make where     print executable path"

build:
	cargo build

release:
	cargo build --release

check:
	cargo check

test:
	cargo test

clean:
	cargo clean

run:
	cargo run --release -- $(FILE)

where:
	@echo "$(abspath target/debug/bobcat-cli)"
	@echo "$(abspath target/release/bobcat-cli)"
