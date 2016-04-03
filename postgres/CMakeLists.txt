include_directories("${PROJECT_BINARY_DIR}/src/")

add_library(postgres SHARED lily_postgres.c)
target_link_libraries(postgres pq)
set_target_properties(
    postgres
    PROPERTIES
        PREFIX ""
)

find_package(PQ)

include_directories(${PQ_INCLUDE_DIRS})
target_link_libraries(postgres ${PQ_LIBRARY})

install(TARGETS postgres DESTINATION lib/lily/0_15/)
