include_directories("${PROJECT_BINARY_DIR}/src/")

add_library(postgre SHARED lily_postgre.c)
target_link_libraries(postgre pq)
set_target_properties(
    postgre
    PROPERTIES
        PREFIX ""
)

find_package(PQ)

include_directories(${PQ_INCLUDE_DIRS})
target_link_libraries(postgre ${PQ_LIBRARY})

install(TARGETS postgre DESTINATION lib/lily/0_13/)
