cmake_minimum_required(VERSION 3.10)
project("Lily" C)

if(MSVC)
    string(REPLACE "/W3" "/W4" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    add_definitions(/wd4201)
    add_definitions(/wd4204)
    add_definitions(/wd4214)
    add_definitions(/MP)
    set(CMAKE_CTEST_COMMAND copy lily.exe ..)
else()
    set(CMAKE_CTEST_COMMAND cp lily ..)
    if(DEBUG)
        set(CMAKE_C_FLAGS      "${CMAKE_C_FLAGS} -g3")
    else()
        set(CMAKE_C_FLAGS      "${CMAKE_C_FLAGS} -O2")
    endif(DEBUG)

    set(CMAKE_C_FLAGS          "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-implicit-fallthrough -Wsign-compare -Wshadow")

    if(WITH_COVERAGE)
        set(CMAKE_C_FLAGS      "${CMAKE_C_FLAGS} -O0 -fprofile-arcs -ftest-coverage")
    endif(WITH_COVERAGE)
endif()

set(LIBRARY_OUTPUT_PATH    "${PROJECT_BINARY_DIR}/lib")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}")

set(LILY_MAJOR             "2")
set(LILY_MINOR             "1")

# BSD libc includes the dl* functions and there's no libdl on them.
# Unfortunately, CMake doesn't seem to distinguish *BSD from the other *nixen.
STRING(REGEX MATCH "BSD" IS_BSD ${CMAKE_SYSTEM_NAME})

if(IS_BSD OR MINGW OR MSVC OR APPLE)
    set(LILY_NEED_DL 0)
else()
    set(LILY_NEED_DL 1)
endif()

# Windows doesn't have a math library to link against.

if(WIN32)
    set(LILY_NEED_M 0)
else()
    set(LILY_NEED_M 1)
endif()

add_subdirectory(src)
add_subdirectory(run)
add_subdirectory(test)

set(TEST_COMMAND cd .. && ./pre-commit-tests)
add_custom_target(check ${CMAKE_CTEST_COMMAND}
  COMMAND ${TEST_COMMAND}
  DEPENDS lily VERBATIM)
