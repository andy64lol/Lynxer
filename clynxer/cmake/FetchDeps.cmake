# Stage cpp-httplib, Crow, and nlohmann/json for the native stdlibs.
# Invoked as: cmake -DCLYNXER_ROOT=... -P cmake/FetchDeps.cmake

if(NOT CLYNXER_ROOT)
  message(FATAL_ERROR "CLYNXER_ROOT must be set")
endif()

set(THIRD_PARTY "${CLYNXER_ROOT}/third_party")
set(STDLIB "${CLYNXER_ROOT}/stdlib")
set(HTTPLIB_DIR "${THIRD_PARTY}/cpp-httplib")
set(CROW_DIR "${THIRD_PARTY}/Crow")
set(JSON_DIR "${THIRD_PARTY}/json")
set(HTTPLIB_HEADER "${STDLIB}/httplib.h")

file(MAKE_DIRECTORY "${THIRD_PARTY}")

find_package(Git REQUIRED)

if(EXISTS "${HTTPLIB_DIR}" AND NOT EXISTS "${HTTPLIB_DIR}/.git")
  message(STATUS "Removing incomplete cpp-httplib checkout...")
  file(REMOVE_RECURSE "${HTTPLIB_DIR}")
endif()

if(NOT EXISTS "${HTTPLIB_DIR}/.git")
  message(STATUS "Cloning cpp-httplib...")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" clone --depth 1
            https://github.com/yhirose/cpp-httplib.git "${HTTPLIB_DIR}"
    RESULT_VARIABLE status
  )
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "git clone cpp-httplib failed")
  endif()
endif()

if(NOT EXISTS "${HTTPLIB_HEADER}")
  message(STATUS "Copying httplib.h into stdlib/")
  file(COPY "${HTTPLIB_DIR}/httplib.h" DESTINATION "${STDLIB}")
endif()

if(EXISTS "${CROW_DIR}" AND NOT EXISTS "${CROW_DIR}/.git")
  message(STATUS "Removing incomplete Crow checkout...")
  file(REMOVE_RECURSE "${CROW_DIR}")
endif()

if(NOT EXISTS "${CROW_DIR}/.git")
  message(STATUS "Cloning Crow...")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" clone --depth 1
            https://github.com/CrowCpp/Crow.git "${CROW_DIR}"
    RESULT_VARIABLE status
  )
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "git clone Crow failed")
  endif()
endif()

if(NOT EXISTS "${CROW_DIR}/include/crow.h")
  message(FATAL_ERROR "Crow clone is missing include/crow.h")
endif()

if(EXISTS "${JSON_DIR}" AND NOT EXISTS "${JSON_DIR}/.git")
  message(STATUS "Removing incomplete nlohmann/json checkout...")
  file(REMOVE_RECURSE "${JSON_DIR}")
endif()

if(NOT EXISTS "${JSON_DIR}/.git")
  message(STATUS "Cloning nlohmann/json...")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" clone --depth 1
            https://github.com/nlohmann/json "${JSON_DIR}"
    RESULT_VARIABLE status
  )
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "git clone nlohmann/json failed")
  endif()
endif()

if(NOT EXISTS "${JSON_DIR}/single_include/nlohmann/json.hpp")
  message(FATAL_ERROR
          "nlohmann/json clone is missing single_include/nlohmann/json.hpp")
endif()

message(STATUS "clynxer deps ready:")
message(STATUS "  httplib -> ${HTTPLIB_HEADER}")
message(STATUS "  Crow    -> ${CROW_DIR}/include")
message(STATUS "  json    -> ${JSON_DIR}/single_include")
