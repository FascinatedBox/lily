include_directories("${PROJECT_SOURCE_DIR}/src/")

add_executable(lily lily.c)
target_link_libraries(lily liblily)
install(TARGETS lily DESTINATION bin)

add_executable(lily_repl lily_repl.c)
target_link_libraries(lily_repl liblily)
install(TARGETS lily_repl DESTINATION bin)
