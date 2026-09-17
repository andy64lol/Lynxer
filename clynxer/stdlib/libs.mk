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
MODULE_PKG_game  := sdl2

MODULE_FLAGS_lua   :=
MODULE_FLAGS_tui   :=
MODULE_FLAGS_image :=
MODULE_FLAGS_game  :=
MODULE_FLAGS_json  := -I$(CURDIR)/third_party/json/single_include
