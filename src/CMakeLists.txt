file(GLOB lily_SOURCES *.c *.h)

# The reason I've called this liblily but removed the prefix is so that I can
# use liblily to build the lily executable.
# -fPIC is vital: Apache's mod_lily will not build properly without it.
add_library(liblily STATIC ${lily_SOURCES})
if(NOT MSVC)
    target_link_libraries(liblily dl)
    set_target_properties(
        liblily
        PROPERTIES
            COMPILE_FLAGS "-fPIC"
            PREFIX ""
    )
endif()

install(FILES lily_api_alloc.h
              lily_api_dynaload.h
              lily_api_options.h
              lily_api_value.h
        DESTINATION "lily")
