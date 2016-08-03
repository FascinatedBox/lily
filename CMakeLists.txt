cmake_minimum_required(VERSION 2.6)
project("Lily")

if(MSVC)
    add_definitions(/W4)
    add_definitions(/wd4201)
    add_definitions(/wd4204)
    add_definitions(/wd4214)
    add_definitions(/MP)
else()
    if(DEBUG)
        set(CMAKE_C_FLAGS      "${CMAKE_C_FLAGS} -g3")
    endif(DEBUG)

    set(CMAKE_C_FLAGS          "${CMAKE_C_FLAGS} -Wall")
endif()

set(CMAKE_MODULE_PATH      "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(LIBRARY_OUTPUT_PATH    "${PROJECT_BINARY_DIR}/lib")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}")

set(LILY_MAJOR             "0")
set(LILY_MINOR             "17")
set(LILY_VERSION_DIR       "${LILY_MAJOR}_${LILY_MINOR}")

add_definitions(-DLILY_VERSION_DIR="${LILY_MAJOR}_${LILY_MINOR}")

option(WITH_APACHE, "Build and install mod_lily for apache" OFF)

add_subdirectory(src)
add_subdirectory(run)

if(WITH_SANDBOX)
    add_subdirectory(sandbox)
endif(WITH_SANDBOX)
if(WITH_APACHE)
    add_subdirectory(apache)
endif(WITH_APACHE)
if(WITH_EMBED_EXAMPLES)
    add_subdirectory(embed_examples)
endif(WITH_EMBED_EXAMPLES)
