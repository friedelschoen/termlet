cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app)

include(${CMAKE_CURRENT_LIST_DIR}/../lib/fs/sysfs/cmake/sysfs.cmake)

sysfs_generate(
    ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

if(CONFIG_BOARD_NATIVE_SIM AND CONFIG_PICOLIBC)
    target_sources(app PRIVATE
        ${ZEPHYR_PICOLIBC_MODULE_DIR}/newlib/libc/machine/x86/setjmp.S
    )
endif()

target_sources(app 
  PRIVATE 
    src/main.c 
    src/devfs.c 
    src/render.c 
    src/net/connection.c 
    src/net/network.c
    src/corelib/corelib.c 
    src/corelib/windowlib.c 
)
