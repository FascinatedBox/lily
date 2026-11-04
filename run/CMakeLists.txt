include_directories("${PROJECT_SOURCE_DIR}/src/")

add_executable(lily lily.c repl.c $<TARGET_OBJECTS:liblily_obj>)
target_compile_definitions(lily
    PUBLIC
    LILY_MAJOR="${LILY_MAJOR}"
    LILY_MINOR="${LILY_MINOR}")
set_target_properties(lily PROPERTIES ENABLE_EXPORTS TRUE)
target_link_libraries(lily ${CMAKE_DL_LIBS})

if(LILY_NEED_M)
    target_link_libraries(lily m)
endif()

install(TARGETS lily DESTINATION bin)
