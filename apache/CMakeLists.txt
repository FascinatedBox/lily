include_directories("${PROJECT_BINARY_DIR}/src/")

add_library(mod_lily SHARED mod_lily.c)
target_link_libraries(mod_lily liblily)
set_target_properties(
    mod_lily
    PROPERTIES
        PREFIX ""
)

# Most of this (including the accompanying cmake/FindX.cmake files is copied
# from apache's mod_mapcache.

find_package(APR)
if(APR_FOUND)
    include_directories(${APR_INCLUDE_DIR} ${APU_INCLUDE_DIR})
    target_link_libraries(mod_lily ${APR_LIBRARY} ${APU_LIBRARY})
    if(DEFINED APR_CPPFLAGS)
        add_definitions(${APR_CPPFLAGS})
    endif(DEFINED APR_CPPFLAGS)
else(APR_FOUND)
    message(FATAL_ERROR "Apache specified, but apr not found.")
endif(APR_FOUND)

find_package(APACHE)
if(APACHE_FOUND)
    include_directories(${APACHE_INCLUDE_DIR})
    target_link_libraries(mod_lily ${APACHE_LIBRARY})
else(APACHE_FOUND)
    message(FATAL_ERROR "Apache specified, but apache not found.")
endif(APACHE_FOUND)

if(APACHE_MODULE_DIR)
    INSTALL(TARGETS mod_lily DESTINATION ${APACHE_MODULE_DIR})
else(APACHE_MODULE_DIR)
    message(FATAL_ERROR "Cannot locate apache module dir")
endif(APACHE_MODULE_DIR)
