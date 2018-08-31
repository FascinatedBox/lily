include_directories("${PROJECT_SOURCE_DIR}/src/")

# The coverage library needs to build in this directory so scripts can load it.
add_library(covlib SHARED covlib.c)
set_target_properties(covlib PROPERTIES
                      PREFIX ""
                      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/")

add_executable(pre-commit-tests extend.c run_tests.c $<TARGET_OBJECTS:liblily_obj>)

if(LILY_NEED_DL)
    target_link_libraries(pre-commit-tests dl)
endif()
