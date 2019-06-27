add_library(packagelib SHARED lily_packagelib.c)
set_target_properties(packagelib PROPERTIES
                      PREFIX ""
                      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/packages/packagelib/src/")
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/test/packages/packagelib/src/")

add_library(brokenlib SHARED lily_packagelib.c)
set_target_properties(brokenlib PROPERTIES
                      PREFIX ""
                      LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test/packages/brokenlib/src/")
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/test/packages/brokenlib/src/")
