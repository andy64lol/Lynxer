cmake_minimum_required(VERSION 3.16)
project(clynxer_stdlib_deps LANGUAGES CXX)

# Fetches and stages the native stdlib dependencies that replace the old
# http/net stack:
#   network  -> cpp-httplib (HTTP client + WebSocket client)
#   server   -> Crow (HTTP/WebSocket server), with Boost.Asio
#   json     -> nlohmann/json (header-only JSON parser)
#
# Usage from the repo root:
#   cmake -S clynxer -B clynxer/build
#   cmake --build clynxer/build --target clynxer_deps
#   cmake --build clynxer/build --target network server
#
# Or simply `make -C clynxer deps` / `make buildCLynxer`.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(POLICY CMP0167)
  # Crow only needs Boost headers. With CMP0167=NEW, CMake removes its
  # built-in FindBoost module and requires a BoostConfig.cmake file, which
  # standard Nix Boost installations do not provide.
  cmake_policy(SET CMP0167 OLD)
endif()

set(CLYNXER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CLYNXER_STDLIB "${CLYNXER_ROOT}/stdlib")
set(CLYNXER_THIRD_PARTY "${CLYNXER_ROOT}/third_party")
set(HTTPLIB_HEADER "${CLYNXER_STDLIB}/httplib.h")
set(CROW_INCLUDE_DIR "${CLYNXER_THIRD_PARTY}/Crow/include")

option(CLYNXER_FETCH_DEPS
       "Clone cpp-httplib, Crow, and nlohmann/json when missing" ON)
option(CLYNXER_BUILD_NETWORK "Build the network stdlib module" ON)
option(CLYNXER_BUILD_SERVER "Build the server stdlib module" ON)

find_package(Threads REQUIRED)
find_package(OpenSSL QUIET)
find_package(Boost 1.64 QUIET)

# Crow is header-only in this project. If a Boost package does not provide a
# CMake config target, the FindBoost module still gives us the include path.
# Keep a small header-only fallback for environments where only the headers
# are discoverable.
if(NOT Boost_FOUND)
  set(CLYNXER_TOOLCHAIN_INCLUDE_DIRS)
  if(DEFINED ENV{NIX_CFLAGS_COMPILE})
    string(REGEX MATCHALL "/[^ \t;]+/include"
           CLYNXER_NIX_INCLUDE_DIRS "$ENV{NIX_CFLAGS_COMPILE}")
    list(APPEND CLYNXER_TOOLCHAIN_INCLUDE_DIRS
         ${CLYNXER_NIX_INCLUDE_DIRS})
  endif()
  find_path(CLYNXER_BOOST_INCLUDE_DIR
    NAMES boost/asio.hpp
    HINTS
      "${Boost_INCLUDE_DIR}"
      "$ENV{BOOST_ROOT}/include"
      "$ENV{BOOST_INCLUDEDIR}"
      ${CLYNXER_TOOLCHAIN_INCLUDE_DIRS}
    PATHS /usr/include /usr/local/include
    PATH_SUFFIXES include
  )
  if(CLYNXER_BOOST_INCLUDE_DIR)
    set(Boost_FOUND TRUE)
    set(Boost_INCLUDE_DIRS "${CLYNXER_BOOST_INCLUDE_DIR}")
    if(NOT TARGET Boost::boost)
      add_library(Boost::boost INTERFACE IMPORTED)
      set_target_properties(Boost::boost PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CLYNXER_BOOST_INCLUDE_DIR}"
      )
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------
# Fetch / stage third-party headers
# ---------------------------------------------------------------------------
add_custom_target(clynxer_clean_third_party
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${CLYNXER_THIRD_PARTY}"
  COMMAND ${CMAKE_COMMAND} -E rm -f "${HTTPLIB_HEADER}"
  COMMENT "Removing Clynxer third-party checkouts and staged headers"
  VERBATIM
)

add_custom_target(clynxer_deps
  COMMAND ${CMAKE_COMMAND}
          -DCLYNXER_ROOT=${CLYNXER_ROOT}
          -P ${CLYNXER_ROOT}/cmake/FetchDeps.cmake
  COMMENT "Fetching cpp-httplib, Crow, and nlohmann/json into clynxer/third_party"
  VERBATIM
)
add_dependencies(clynxer_deps clynxer_clean_third_party)

if(CLYNXER_FETCH_DEPS)
  if(NOT EXISTS "${HTTPLIB_HEADER}" OR NOT EXISTS "${CROW_INCLUDE_DIR}/crow.h")
    execute_process(
      COMMAND ${CMAKE_COMMAND}
              -DCLYNXER_ROOT=${CLYNXER_ROOT}
              -P ${CLYNXER_ROOT}/cmake/FetchDeps.cmake
      RESULT_VARIABLE _fetch_status
    )
    if(NOT _fetch_status EQUAL 0)
      message(FATAL_ERROR "Failed to fetch clynxer native stdlib dependencies")
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------
# network.so — cpp-httplib HTTP + WebSocket client
# ---------------------------------------------------------------------------
if(CLYNXER_BUILD_NETWORK)
  if(NOT EXISTS "${HTTPLIB_HEADER}")
    message(WARNING "clynxer: skipping network.so (httplib.h missing; run clynxer_deps)")
  elseif(NOT OpenSSL_FOUND)
    message(WARNING "clynxer: skipping network.so (OpenSSL not found)")
  else()
    add_library(network MODULE "${CLYNXER_STDLIB}/network.cpp")
    set_target_properties(network PROPERTIES
      PREFIX ""
      SUFFIX ".so"
      LIBRARY_OUTPUT_DIRECTORY "${CLYNXER_STDLIB}"
      RUNTIME_OUTPUT_DIRECTORY "${CLYNXER_STDLIB}"
    )
    target_include_directories(network PRIVATE "${CLYNXER_STDLIB}")
    target_compile_definitions(network PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_compile_options(network PRIVATE -fPIC -Wall -Wextra -pedantic)
    target_link_libraries(network PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    add_dependencies(network clynxer_deps)
  endif()
endif()

# ---------------------------------------------------------------------------
# server.so — Crow HTTP/WebSocket server
# ---------------------------------------------------------------------------
if(CLYNXER_BUILD_SERVER)
  if(NOT EXISTS "${CROW_INCLUDE_DIR}/crow.h")
    message(WARNING "clynxer: skipping server.so (Crow missing; run clynxer_deps)")
  elseif(NOT Boost_FOUND)
    message(WARNING "clynxer: skipping server.so (Boost not found)")
  else()
    add_library(server MODULE "${CLYNXER_STDLIB}/server.cpp")
    set_target_properties(server PROPERTIES
      PREFIX ""
      SUFFIX ".so"
      LIBRARY_OUTPUT_DIRECTORY "${CLYNXER_STDLIB}"
      RUNTIME_OUTPUT_DIRECTORY "${CLYNXER_STDLIB}"
    )
    target_include_directories(server PRIVATE "${CROW_INCLUDE_DIR}")
    target_compile_definitions(server PRIVATE CROW_USE_BOOST)
    target_compile_options(server PRIVATE -fPIC -Wall -Wextra -pedantic)
    target_link_libraries(server PRIVATE Boost::boost Threads::Threads)
    add_dependencies(server clynxer_deps)
  endif()
endif()
