include_directories("${PROJECT_SOURCE_DIR}/src/")

add_executable(lily lily.c $<TARGET_OBJECTS:liblily_obj>)

if(LILY_NEED_M)
    target_link_libraries(lily m)
endif()

if(LILY_NEED_DL)
    target_link_libraries(lily dl)
endif()

install(TARGETS lily DESTINATION bin)
