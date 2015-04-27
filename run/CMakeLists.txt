include_directories("${PROJECT_SOURCE_DIR}/src/")

add_executable(lily lily.c)
target_link_libraries(lily liblily)
install(TARGETS lily DESTINATION bin)
