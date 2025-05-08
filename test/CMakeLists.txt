include_directories("${PROJECT_SOURCE_DIR}/src/")

function(set_output_dirs TARGET_NAME PATH)
    # Extra dirs are set for multi-config generators (like MSBuild) to make
    # sure they build to the right directory.
    set_target_properties("${TARGET_NAME}" PROPERTIES
                          PREFIX ""
                          LIBRARY_OUTPUT_DIRECTORY         "${PATH}"
                          LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${PATH}"
                          LIBRARY_OUTPUT_DIRECTORY_RELEASE "${PATH}"
                          RUNTIME_OUTPUT_DIRECTORY         "${PATH}"
                          RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${PATH}"
                          RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PATH}")
endfunction(set_output_dirs)

set(backbone_SOURCES
    t/src/lily_backbone.c
    t/src/lily_covlib.c
    t/src/lily_farm.c)

if(WIN32 OR APPLE)
    add_library(backbone SHARED ${backbone_SOURCES} $<TARGET_OBJECTS:liblily_obj>)
else()
    add_library(backbone SHARED ${backbone_SOURCES})
endif()

add_executable(pre-commit-tests test_driver.c $<TARGET_OBJECTS:liblily_obj>)
set_target_properties(pre-commit-tests PROPERTIES ENABLE_EXPORTS TRUE)

if(LILY_NEED_DL)
    target_link_libraries(pre-commit-tests dl)
endif()

if(LILY_NEED_M)
    target_link_libraries(pre-commit-tests m)
endif()

set_output_dirs(backbone         "${PROJECT_BINARY_DIR}/test/t/")
set_output_dirs(pre-commit-tests "${PROJECT_BINARY_DIR}")
