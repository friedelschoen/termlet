zephyr_library()

zephyr_library_compile_options(
    -ffast-math
    -fno-strict-aliasing
)

zephyr_library_compile_definitions(
    _GNU_SOURCE
    _POSIX_C_SOURCE=200809L
)

zephyr_include_directories(include)

zephyr_library_sources(
    tsm-render.c
    tsm-screen.c
    tsm-selection.c
    tsm-unicode.c
    tsm-vte-charsets.c
    tsm-vte.c

    shl-htable.c
)
