include_directories("${PROJECT_SOURCE_DIR}/src/")

add_executable(call_lily_from_c call_lily_from_c.c)
target_link_libraries(call_lily_from_c liblily)

# Keep examples here, so they don't pollute the toplevel dir.
set_target_properties(call_lily_from_c
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/embed_examples")
