cmake_minimum_required(VERSION 2.6)
project("Lily")

if(DEBUG)
    set(CMAKE_C_FLAGS      "${CMAKE_C_FLAGS} -g3")
endif(DEBUG)

set(CMAKE_C_FLAGS          "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_MODULE_PATH      "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(LIBRARY_OUTPUT_PATH    "${PROJECT_BINARY_DIR}/lib")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}")

option(WITH_APACHE, "Build and install mod_lily for apache" OFF)

add_subdirectory(src)
add_subdirectory(run)
if(WITH_POSTGRE)
    add_subdirectory(postgre)
endif(WITH_POSTGRE)
if(WITH_APACHE)
    add_subdirectory(apache)
endif(WITH_APACHE)
