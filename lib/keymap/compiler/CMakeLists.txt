cmake_minimum_required(VERSION 3.20)

project(genkeymap-compile C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

add_executable(genkeymap-compile
    compile.c
    utf8_map.c
    compose_tree.c
)

target_link_libraries(genkeymap-compile PRIVATE
    PkgConfig::XKBCOMMON
)
