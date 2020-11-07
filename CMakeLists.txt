cmake_minimum_required(VERSION 3.0.2)
project("Lily" C)

if(MSVC)
    # MSBuild adds the configuration kind (Release/Debug/etc.) as a suffix
    # directory to libraries and executables. This causes the testing suite to
    # fail because the libraries aren't where it expects them to be.
    # GENERATOR_IS_MULTI_CONFIG might be usable here instead, but that requires
    # a much more recent version of cmake.
    if(NOT "${CMAKE_VS_MSBUILD_COMMAND}" STREQUAL "")
        message(FATAL_ERROR
                "Cannot build Lily using MSBuild (try -GNinja or another generator instead).")
    endif()

    add_definitions(/W4)
    add_definitions(/wd4201)
    add_definitions(/wd4204)
    add_definitions(/wd4214)
    add_definitions(/MP)
    set(CMAKE_CTEST_COMMAND	copy lily.exe ..)
else()
    set(CMAKE_CTEST_COMMAND	cp lily ..)
    if(DEBUG)
        set(CMAKE_C_FLAGS      "${CMAKE_C_FLAGS} -g3")
    endif(DEBUG)

    if(NOT QUICK_BUILD)
        set(CMAKE_C_FLAGS      "${CMAKE_C_FLAGS} -O2")
    endif(NOT QUICK_BUILD)

    set(CMAKE_C_FLAGS          "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-implicit-fallthrough -Wsign-compare -Wshadow")

    if(WITH_COVERAGE)
        set(CMAKE_C_FLAGS      "${CMAKE_C_FLAGS} -O0 -fprofile-arcs -ftest-coverage")
    endif(WITH_COVERAGE)
endif()

set(CMAKE_MODULE_PATH      "${PROJECT_SOURCE_DIR}/cmake")
set(LIBRARY_OUTPUT_PATH    "${PROJECT_BINARY_DIR}/lib")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}")

set(LILY_MAJOR             "1")
set(LILY_MINOR             "14")

add_definitions(-DLILY_VERSION_DIR="${LILY_MAJOR}_${LILY_MINOR}")

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

if(WITH_SANDBOX)
    if("$ENV{EMSCRIPTEN}" STREQUAL "")
        message(FATAL_ERROR
        "The sandbox can't be built without Emscripten. Did you forget to source emsdk_env?")
    endif()
    add_subdirectory(sandbox)
endif(WITH_SANDBOX)

if(WITH_COVERAGE)
    if(CMAKE_COMPILER_IS_GNUCXX)
        include(CodeCoverage)
        setup_target_for_coverage(${PROJECT_NAME}_coverage tests coverage)
    endif()

    include(CTest)
    enable_testing()
    add_test(NAME pre-commit-tests COMMAND pre-commit-tests)
endif(WITH_COVERAGE)

set(TEST_COMMAND cd .. && ./pre-commit-tests)
add_custom_target(check ${CMAKE_CTEST_COMMAND}
  COMMAND ${TEST_COMMAND}
  DEPENDS lily VERBATIM)
