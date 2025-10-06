file(GLOB lily_SOURCES *.c *.h)

# The objects in this library are used to build several targets, but the library
# itself is never installed.
add_library(liblily_obj OBJECT ${lily_SOURCES})

target_compile_definitions(liblily_obj
    PUBLIC
    LILY_MAJOR="${LILY_MAJOR}"
    LILY_MINOR="${LILY_MINOR}")

# This is needed for Lily to work properly as a shared library. (This probably
# doesn't do anything on MSVC, but there's no harm in setting it anyway.)
set_target_properties(liblily_obj PROPERTIES
                      POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})

# This is required on Windows for C extensions that use interpreter functions,
# if we're building as a shared library. Said functions aren't marked with
# __declspec(dllimport); an import library won't even be created if we don't do
# this, preventing extensions from linking to Lily at all!
#
# (This has to be done before the liblily target is created to apply to it.)
if(WIN32 AND BUILD_SHARED_LIBS)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

add_library(liblily $<TARGET_OBJECTS:liblily_obj>)

if(NOT WIN32)
    # This prevents the library being named libliblily.
    set_target_properties(liblily PROPERTIES PREFIX "")
endif()

if(LILY_NEED_DL)
    target_link_libraries(liblily dl)
endif()

if(LILY_NEED_M)
    target_link_libraries(liblily m)
endif()

install(TARGETS liblily
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        COMPONENT library)

install(FILES lily.h
        DESTINATION "include/lily")
