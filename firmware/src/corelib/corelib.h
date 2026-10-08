#pragma once

#include <lua.h>
#include <window/layout.h>

extern struct win_layout layout;

void preload_register(lua_State *L, const char *name, lua_CFunction openf);

void windowlib_register(lua_State *L);
void corelib_register(lua_State *L);
