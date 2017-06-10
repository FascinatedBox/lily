SET(CMAKE_C_COMPILER   emcc)
SET(CMAKE_CXX_COMPILER em++)
SET(CMAKE_AR           emar)
SET(CMAKE_RANLIB       emranlib)

SET(MY_EXPORTS         "'_get_parser', '_run_parser', '_get_parser_error'")
SET(MY_EXPORTS         "${MY_EXPORTS}, '_destroy_parser'")

SET(CMAKE_C_FLAGS      "-O1 -Wno-warn-absolute-paths --memory-init-file 0 -s EXPORTED_FUNCTIONS=\"[${MY_EXPORTS}]\"")
SET(CMAKE_CXX_FLAGS    -g)
SET(CMAKE_TOOLCHAIN_FILE  "$EMSCRIPTEN/cmake/Platform/Emscripten.cmake")
SET(CMAKE_EXECUTABLE_SUFFIX ".js")

file(GLOB sandbox_SOURCES ../src/*.c)
include_directories("${PROJECT_SOURCE_DIR}/src/")

set(CMAKE_MODULE_PATH      "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(LIBRARY_OUTPUT_PATH    "${PROJECT_BINARY_DIR}/lib")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}")

add_executable(lily_sandbox ${sandbox_SOURCES} sandbox.c)
