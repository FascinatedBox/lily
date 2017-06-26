include_directories("${PROJECT_SOURCE_DIR}/src/")

add_executable(pre-commit-tests extend.c run_tests.c $<TARGET_OBJECTS:liblily_obj>)

if(LILY_NEED_DL)
    target_link_libraries(pre-commit-tests dl)
endif()
