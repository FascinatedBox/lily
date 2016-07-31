file(GLOB lily_SOURCES *.c *.h)

# BSD libc includes the dl* functions and there's no libdl on them.
# Unfortunately, CMake doesn't seem to distinguish *BSD from the other *nixen.
STRING(REGEX MATCH "BSD" IS_BSD ${CMAKE_SYSTEM_NAME})

# The reason I've called this liblily but removed the prefix is so that I can
# use liblily to build the lily executable.
# -fPIC is vital: Apache's mod_lily will not build properly without it.
add_library(liblily STATIC ${lily_SOURCES})
if(NOT MSVC)
    if(IS_BSD OR MINGW)
        target_link_libraries(liblily)
    else()
        target_link_libraries(liblily dl)
    endif()
    set_target_properties(
        liblily
        PROPERTIES
            COMPILE_FLAGS "-fPIC"
            PREFIX ""
    )
endif()

install(FILES lily_api_alloc.h
              lily_api_embed.h
              lily_api_options.h
              lily_api_msgbuf.h
              lily_api_value.h
        DESTINATION "lily")
