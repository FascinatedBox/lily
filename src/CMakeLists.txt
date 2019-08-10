file(GLOB lily_SOURCES *.c *.h)

# The goal is to have a lily executable that's standalone, and a shared liblily
# library. The two use the same sources, put together in an object library.
add_library(liblily_obj OBJECT ${lily_SOURCES})
add_library(liblily SHARED $<TARGET_OBJECTS:liblily_obj>)

if(NOT MSVC)
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
              lily_int_code_iter.h
              lily_int_opcode.h
        DESTINATION "include/lily")
