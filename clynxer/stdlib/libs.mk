# Extra link flags for C++ stdlib modules (clynxer/stdlib/*.cpp ->
# clynxer/stdlib/*.so), appended by the `stdlib/%.so` rule in the Makefile.
#
# MODULE_FLAGS_<name>
#   Raw compiler/linker flags for that module.
#
# json, network and server are Rust crates under clynxer/rust/ and are built by
# the Makefile's cargo rules, not by this table. `game` is a C++ shim that links
# the Rust + macroquad static library, so it does need flags here.

MODULE_FLAGS_game := -Wl,--whole-archive $(CURDIR)/build/rust/release/libclynxer_game.a -Wl,--no-whole-archive -lpthread -ldl -lm
