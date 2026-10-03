cmake_minimum_required(VERSION 3.20)

find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

set(KEYMAP_GENERATOR
    ${CMAKE_CURRENT_BINARY_DIR}/genkeymap-compile
)

set(KEYMAP_SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/generated_keymap.c
)

#
# Host-side generator
#
add_executable(genkeymap-compile
    compiler/compile.c
    compiler/utf8_map.c
    compiler/compose_tree.c
)

target_link_libraries(genkeymap-compile PRIVATE
    PkgConfig::XKBCOMMON
)

#
# Generate the embedded keymap source.
#
add_custom_command(
    OUTPUT
        ${KEYMAP_SOURCE}

    COMMAND
        $<TARGET_FILE:genkeymap-compile>
        --rules evdev
        --model pc105
        --layout ${CONFIG_KEYMAP_KEYBOARD_LAYOUT}
        --variant ${CONFIG_KEYMAP_KEYBOARD_VARIANT}
        --locale ${CONFIG_KEYMAP_KEYBOARD_LOCALE}
        --header keymap.h
        --int-header keymap-int.h
        > ${KEYMAP_SOURCE}

    DEPENDS
        genkeymap-compile
     
    VERBATIM
)

#
# Zephyr library
#
zephyr_library()

zephyr_library_sources(
    ${KEYMAP_SOURCE}
    keymap.c
)

zephyr_library_include_directories(.)
zephyr_include_directories(include)
