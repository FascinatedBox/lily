include_directories("${PROJECT_SOURCE_DIR}/src/")

# The coverage library needs to build in this directory so scripts can load it.

if(NOT WIN32)
    add_library(covlib SHARED lily_covlib.c)
else()
    add_library(covlib SHARED lily_covlib.c $<TARGET_OBJECTS:liblily_obj>)
endif()

set_target_properties(covlib PROPERTIES
                      PREFIX ""
                      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/"
                      RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/")

add_subdirectory(libraries)

add_executable(pre-commit-tests lily_extend.c run_tests.c $<TARGET_OBJECTS:liblily_obj>)

if(LILY_NEED_DL)
    target_link_libraries(pre-commit-tests dl)
endif()

if(LILY_NEED_M)
    target_link_libraries(pre-commit-tests m)
endif()
