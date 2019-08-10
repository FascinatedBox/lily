if(NOT WIN32)
    add_library(packagelib SHARED lily_packagelib.c)
    add_library(brokenlib SHARED lily_packagelib.c)
else()
    add_library(packagelib SHARED lily_packagelib.c $<TARGET_OBJECTS:liblily_obj>)
    add_library(brokenlib SHARED lily_packagelib.c $<TARGET_OBJECTS:liblily_obj>)
endif()

set_target_properties(packagelib PROPERTIES
                      PREFIX ""
                      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/packages/packagelib/src/"
                      RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/packages/packagelib/src/")
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/test/packages/packagelib/src/")

set_target_properties(brokenlib PROPERTIES
                      PREFIX ""
                      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/packages/brokenlib/src/"
                      RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/packages/packagelib/src/")

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/test/packages/brokenlib/src/")
