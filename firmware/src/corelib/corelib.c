#include "corelib.h"

#include <lauxlib.h>


void preload_register(lua_State *L, const char *name, lua_CFunction openf) {
	lua_getfield(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
	lua_pushcfunction(L, openf);
	lua_setfield(L, -2, name);
	lua_pop(L, 1);
}

void corelib_register(lua_State *L) {
	windowlib_register(L);
}
