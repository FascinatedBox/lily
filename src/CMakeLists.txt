file(GLOB lily_SOURCES *.c *.h)

# The objects in this library are used to build several targets, but the library
# itself is never installed.
add_library(liblily_obj OBJECT ${lily_SOURCES})

target_compile_definitions(liblily_obj
    PUBLIC
    LILY_MAJOR="${LILY_MAJOR}"
    LILY_MINOR="${LILY_MINOR}")

if(WIN32)
    # C extensions that use interpreter functions need to have their own copy of
    # interpreter functions due to how DLLs work. This builds that.
    add_library(liblily STATIC $<TARGET_OBJECTS:liblily_obj>)
else()
    add_library(liblily SHARED $<TARGET_OBJECTS:liblily_obj>)

    if(LILY_NEED_DL)
        target_link_libraries(liblily dl)
    endif()

    # -fPIC is needed for it to work properly as a shared library.
    set_target_properties(liblily_obj PROPERTIES COMPILE_FLAGS "-fPIC")

    # This prevents the shared library being named libliblily
    set_target_properties(liblily PROPERTIES PREFIX "")
endif()

if(LILY_NEED_M)
    target_link_libraries(liblily m)
endif()

install(TARGETS liblily
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        COMPONENT library)

install(FILES lily.h
        DESTINATION "include/lily")
