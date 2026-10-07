# Must be called "Kconfig.<something>" otherwise Zephyr considers 
# this directory as root and does not include ./lib/ !

config LUA_HEAP_SIZE
  int "Heap Lua can consume"
  default 131072  # 128kB

config SYSFS_MAX_FDS
  int 
  default 8
