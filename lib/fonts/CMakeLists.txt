find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GENERATED_FONTS_C
    ${CMAKE_CURRENT_BINARY_DIR}/generated_fonts.c
)

set(FONT_FILES
    ${WEST_TOPDIR}/modules/fonts/go/font/gofont/ttfs/Go-Mono.ttf
    ${WEST_TOPDIR}/modules/fonts/go/font/gofont/ttfs/Go-Mono-Bold.ttf
    ${WEST_TOPDIR}/modules/fonts/go/font/gofont/ttfs/Go-Mono-Bold-Italic.ttf
    ${WEST_TOPDIR}/modules/fonts/go/font/gofont/ttfs/Go-Mono-Italic.ttf
)

add_custom_command(
    OUTPUT ${GENERATED_FONTS_C}
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/genfonts.py
       ${FONT_FILES}
        > ${GENERATED_FONTS_C}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/genfonts.py
        ${FONT_FILES}
    VERBATIM
)

add_custom_target(generate_fonts
    DEPENDS ${GENERATED_FONTS_C}
)

zephyr_library()

zephyr_library_sources(
    fonts.c
    ${GENERATED_FONTS_C}
)

zephyr_include_directories(.)

add_dependencies(${ZEPHYR_CURRENT_LIBRARY} generate_fonts)
