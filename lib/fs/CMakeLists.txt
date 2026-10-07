add_subdirectory_ifdef(CONFIG_SYSFS sysfs)
add_subdirectory_ifdef(CONFIG_DEVFS devfs)

zephyr_include_directories(.)
