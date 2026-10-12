#pragma once

#include <lauxlib.h>
#include <lua.h>

struct lua_callback {
	lua_State *L;
	int ref;
};

static inline void lua_callback_init(struct lua_callback *cb) {
	cb->L = NULL;
	cb->ref = LUA_NOREF;
}

static inline void lua_callback_clear(struct lua_callback *cb) {
	if (cb->L != NULL && cb->ref != LUA_NOREF)
		luaL_unref(cb->L, LUA_REGISTRYINDEX, cb->ref);

	cb->L = NULL;
	cb->ref = LUA_NOREF;
}

static inline void lua_callback_set(lua_State *L, struct lua_callback *cb, int arg) {
	luaL_checktype(L, arg, LUA_TFUNCTION);

	lua_callback_clear(cb);

	lua_pushvalue(L, arg);
	cb->ref = luaL_ref(L, LUA_REGISTRYINDEX);
	cb->L = L;
}

static inline void lua_callback_push(struct lua_callback *cb) {
	lua_rawgeti(cb->L, LUA_REGISTRYINDEX, cb->ref);
};
