set(SYSFS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../generate.py")

function(sysfs_generate) 
    cmake_parse_arguments(
        SYSFS
        ""
        "ROOT;VAR"
        ""
        ${ARGN}
    )

    if(NOT SYSFS_ROOT)
        set(SYSFS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    if(NOT SYSFS_VAR)
        set(SYSFS_VAR "sysfs_root")
    endif()

    set(output "${CMAKE_CURRENT_BINARY_DIR}/${SYSFS_VAR}.c")

    file(
        GLOB_RECURSE sysfs_inputs
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${SYSFS_ROOT}/*"
    )

    add_custom_command(
        OUTPUT "${output}"
        COMMAND
            ${Python3_EXECUTABLE}
            "${SYSFS_SCRIPT}"
            --varname "${SYSFS_VAR}"
            --output "${output}"
            "${SYSFS_ROOT}"
        DEPENDS
            "${SYSFS_SCRIPT}"
            ${sysfs_inputs}
        COMMENT "Generating ${SYSFS_VAR}.c"
        VERBATIM
    )

    # Tell CMake explicitly that this file does not exist at configure time.
    set_source_files_properties(
        "${output}"
        PROPERTIES GENERATED TRUE
    )

    # Explicit build dependency.
    set(target "sysfs_generate_${SYSFS_VAR}")

    add_custom_target(
        "${target}"
        DEPENDS "${output}"
    )

    add_dependencies(app "${target}")
    target_sources(app PRIVATE "${output}")
endfunction()
