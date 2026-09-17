cmake_minimum_required(VERSION 3.16)
project(clynxer_stdlib_deps LANGUAGES CXX)

# Fetches and stages the native stdlib dependencies that replace the old
# http/net stack:
#   network  -> cpp-httplib (HTTP client + WebSocket client)
#   server   -> Crow (HTTP/WebSocket server), with Boost.Asio
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
  cmake_policy(SET CMP0167 NEW)
endif()

set(CLYNXER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CLYNXER_STDLIB "${CLYNXER_ROOT}/stdlib")
set(CLYNXER_THIRD_PARTY "${CLYNXER_ROOT}/third_party")
set(HTTPLIB_HEADER "${CLYNXER_STDLIB}/httplib.h")
set(CROW_INCLUDE_DIR "${CLYNXER_THIRD_PARTY}/Crow/include")

option(CLYNXER_FETCH_DEPS "Clone cpp-httplib and Crow when missing" ON)
option(CLYNXER_BUILD_NETWORK "Build the network stdlib module" ON)
option(CLYNXER_BUILD_SERVER "Build the server stdlib module" ON)

find_package(Threads REQUIRED)
find_package(OpenSSL)
find_package(Boost 1.64 CONFIG QUIET)
if(NOT Boost_FOUND)
  find_package(Boost 1.64)
endif()

# ---------------------------------------------------------------------------
# Fetch / stage third-party headers
# ---------------------------------------------------------------------------
add_custom_target(clynxer_deps
  COMMAND ${CMAKE_COMMAND}
          -DCLYNXER_ROOT=${CLYNXER_ROOT}
          -P ${CLYNXER_ROOT}/cmake/FetchDeps.cmake
  COMMENT "Fetching cpp-httplib and Crow into clynxer/third_party"
  VERBATIM
)

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
