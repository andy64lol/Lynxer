# Per-module native build configuration for clynxer/stdlib/*.cpp.
#
# MODULE_PKG_<name>
#   Space-separated pkg-config packages the module needs to compile and link.
#   Modules listed in OPTIONAL_MODULE_NAMES (see Makefile) are skipped with a
#   warning when their packages cannot be resolved, so a plain `make` never
#   fails just because an opt-in library is missing.
#
# MODULE_FLAGS_<name>
#   Extra raw compiler/linker flags appended after the pkg-config flags. Use
#   this only when pkg-config does not describe the full requirement.
#
# network / server are built through CMake (see CMakeLists.txt) after
# `make deps` stages cpp-httplib, Crow, and nlohmann/json under third_party/.

MODULE_PKG_lua   := lua5.4
MODULE_PKG_tui   := ncursesw
MODULE_PKG_image := libpng zlib

MODULE_FLAGS_lua   :=
MODULE_FLAGS_tui   :=
MODULE_FLAGS_image :=
# The game backend is a Rust static library (see GAME_STATICLIB in the
# Makefile) rather than a pkg-config package. --whole-archive keeps every
# exported op symbol, and the interpreter resolves them from game.so with
# dlsym.
MODULE_FLAGS_game  := -Wl,--whole-archive $(CURDIR)/$(GAME_STATICLIB) -Wl,--no-whole-archive -lpthread -ldl -lm
MODULE_FLAGS_json  := -I$(CURDIR)/third_party/json/single_include
