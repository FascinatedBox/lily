# Try to find where Lily has been installed.
#
# The following variables are defined:
#
# LILY_FOUND        - Boolean indicating if Lily was found.
# LILY_INCLUDE_DIRS - Path to where lily.h is.
#
# The following functions are defined:
#
# lily_add_library(name, targets...)
#     Create an extension library for Lily. Under the hood, this calls
#     `add_library(name SHARED targets)`, and sets common options that most
#     extension libraries will need. Most importantly, this links to liblily.lib
#     on Windows. If that is not done, the resulting dll will not work.
#
#     This function is intended for projects that export a single extension
#     library for the interpreter to load.

set(_LILY_BASE_PATH "${CMAKE_INSTALL_PREFIX}")

if(WIN32)
    set(_LILY_BASE_PATH "${_LILY_BASE_PATH}/../lily")

    find_library(_LILY_STATIC_LIBRARY
                 NAMES
                     "liblily.lib"
                 PATHS
                     "${_LILY_BASE_PATH}/lib")
endif()

find_path(LILY_INCLUDE_DIRS
          NAMES
              lily.h
          PATHS
              "${_LILY_BASE_PATH}/include/lily")

if(WIN32)
    if(LILY_INCLUDE_DIRS AND _LILY_STATIC_LIBRARY)
        set(LILY_FOUND true)
    endif()
else()
    if(LILY_INCLUDE_DIRS)
        set(LILY_FOUND true)
    endif()
endif()

if(NOT LILY_FOUND)
    message(FATAL_ERROR "Could not find Lily.")
endif()

function(lily_add_library _LIBRARY_NAME TARGETS)
    set(_LIBRARY_SOURCES ${ARGV})
    list(REMOVE_AT _LIBRARY_SOURCES 0)

    add_library("${_LIBRARY_NAME}" SHARED "${_LIBRARY_SOURCES}")

    if(WIN32)
        target_link_libraries("${_LIBRARY_NAME}" "${_LILY_STATIC_LIBRARY}")
    endif()

    target_include_directories("${_LIBRARY_NAME}" PUBLIC "${LILY_INCLUDE_DIRS}")

    set_target_properties("${_LIBRARY_NAME}" PROPERTIES
            PREFIX
                ""
            LIBRARY_OUTPUT_DIRECTORY
                "${PROJECT_BINARY_DIR}/src"
    )
endfunction(lily_add_library)
