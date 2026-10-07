zephyr_library()

zephyr_include_directories(.)

zephyr_library_sources(
    lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c luaf.c
    lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loadlib.c lstrlib.c ltablib.c lutf8lib.c linit.c 
)

# not included: loslib.c
