cmake_minimum_required(VERSION 3.20)

include(ExternalProject)

set(KEYMAP_HOST_BUILD_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/genkeymap-build
)

set(KEYMAP_GENERATOR
    ${KEYMAP_HOST_BUILD_DIR}/genkeymap-compile
)

set(KEYMAP_SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/generated_keymap.c
)

find_program(HOST_CC NAMES cc gcc clang REQUIRED)

ExternalProject_Add(genkeymap-host
    SOURCE_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/compiler

    BINARY_DIR
        ${KEYMAP_HOST_BUILD_DIR}

    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=${HOST_CC}

    INSTALL_COMMAND ""

    BUILD_BYPRODUCTS
        ${KEYMAP_GENERATOR}
)

#
# Generate target-side keymap source
#
add_custom_command(
    OUTPUT
        ${KEYMAP_SOURCE}

    COMMAND
        ${KEYMAP_GENERATOR}
        --rules evdev
        --model pc105
        --layout ${CONFIG_KEYMAP_KEYBOARD_LAYOUT}
        --variant ${CONFIG_KEYMAP_KEYBOARD_VARIANT}
        --locale ${CONFIG_KEYMAP_KEYBOARD_LOCALE}
        --header keymap.h
        --int-header keymap-int.h
        > ${KEYMAP_SOURCE}

    DEPENDS
        genkeymap-host

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
