cmake_minimum_required(VERSION 3.23)

project(termlet-tools LANGUAGES C)

execute_process(
    COMMAND west list minitelnet -f "{abspath}"
    OUTPUT_VARIABLE MINITELNET_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "minitelnet: ${MINITELNET_DIR}")

set(MINITELNET_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(MINITELNET_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${MINITELNET_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/minitelnet"
)

add_executable(termlet-run
    src/termlet-run.c
)

target_link_libraries(termlet-run
    PRIVATE
        minitelnet::minitelnet
)
