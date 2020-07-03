include_directories("${PROJECT_SOURCE_DIR}/src/")

set(backbone_SOURCES
    t/lily_backbone.c
    t/lily_covlib.c)

if(NOT WIN32)
    add_library(backbone SHARED ${backbone_SOURCES})
else()
    add_library(backbone SHARED ${backbone_SOURCES} $<TARGET_OBJECTS:liblily_obj>)
endif()

set_target_properties(backbone PROPERTIES
                      PREFIX ""
                      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/t/"
                      RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/t/")

add_executable(pre-commit-tests test_driver.c $<TARGET_OBJECTS:liblily_obj>)

if(LILY_NEED_DL)
    target_link_libraries(pre-commit-tests dl)
endif()

if(LILY_NEED_M)
    target_link_libraries(pre-commit-tests m)
endif()
