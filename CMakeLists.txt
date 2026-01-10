cmake_minimum_required(VERSION 3.10)
project("Lily" C)

# The compiler must be at least this tall (lily_expr uses C11 anonymous unions).
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    string(REPLACE "/W3" "/W4" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    add_definitions(/wd4201)
    add_definitions(/wd4204)
    add_definitions(/wd4214)
    add_definitions(/MP)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
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
set(LILY_MINOR             "3")

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

option("BUILD_SHARED_LIBS" "Build Lily as a shared library." ON)
add_subdirectory(src)

option("LILY_BUILD_EXECUTABLE" "Build the standalone Lily executable." ON)
option("LILY_BUILD_TESTS" "Build Lily's test suite." ON)

if(LILY_BUILD_EXECUTABLE)
    add_subdirectory(run)
endif()

if(LILY_BUILD_TESTS)
    add_subdirectory(test)

    if(WIN32)
        set(TEST_COMMAND pre-commit-tests.exe)
    else()
        set(TEST_COMMAND ./pre-commit-tests)
    endif()

    add_custom_target(check ${TEST_COMMAND}
                      VERBATIM
                      DEPENDS backbone pre-commit-tests)
endif()
