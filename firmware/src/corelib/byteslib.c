#include "corelib.h"

#include <lauxlib.h>
#include <lua.h>
#include <stdlib.h>

#define LUA_BYTES_MT "termlet.bytes"

struct lua_bytes {
	uint8_t *buffer;
	size_t size;

	void (*free)(void *ptr);
};

static struct lua_bytes *bytes_check(lua_State *L, int idx) {
	return luaL_checkudata(L, idx, LUA_BYTES_MT);
}

static int bytes_gc(lua_State *L) {
	struct lua_bytes *buf = bytes_check(L, 1);

	if (buf->free && buf->buffer) {
		buf->free(buf->buffer);
		buf->buffer = NULL;
		buf->size = 0;
	}

	return 0;
}

static int bytes_len(lua_State *L) {
	struct lua_bytes *buf = bytes_check(L, 1);

	lua_pushinteger(L, buf->size);
	return 1;
}

static int bytes_tostring_meta(lua_State *L) {
	struct lua_bytes *buf = bytes_check(L, 1);

	lua_pushlstring(L, (const char *) buf->buffer, buf->size);
	return 1;
}

static struct lua_bytes *bytes_new(lua_State *L) {
	struct lua_bytes *buf = lua_newuserdatauv(L, sizeof(*buf), 0);

	luaL_setmetatable(L, LUA_BYTES_MT);

	return buf;
}

static int bytes_fromstring(lua_State *L) {
	if (lua_type(L, 1) == LUA_TSTRING) {
		size_t len;
		const char *s = lua_tolstring(L, 1, &len);

		struct lua_bytes *out = bytes_new(L);
		out->size = strlen(s);
		out->buffer = malloc(out->size);
		out->free = free; /* not borrowed, free after use. */
		if (!out->buffer)
			return luaL_error(L, "unable to allocate userdata");
		memcpy(out->buffer, s, len);

		return 1;
	}

	if (luaL_testudata(L, 1, LUA_BYTES_MT)) {
		lua_pushvalue(L, 1);
		return 1;
	}

	return luaL_argerror(L, 1, "expected string or bytes");
}

static int bytes_tostring(lua_State *L) {
	if (lua_type(L, 1) == LUA_TSTRING) {
		lua_pushvalue(L, 1);
		return 1;
	}

	struct lua_bytes *buf = luaL_testudata(L, 1, LUA_BYTES_MT);
	if (!buf)
		return luaL_argerror(L, 1, "expected string or bytes");

	lua_pushlstring(L, (const char *) buf->buffer, buf->size);
	return 1;
}

static const luaL_Reg byteslib[] = {
	{ "fromstring", bytes_fromstring },
	{ "tostring", bytes_tostring },
	{ NULL, NULL }
};

static int luaopen_byteslib(lua_State *L) {
	if (luaL_newmetatable(L, LUA_BYTES_MT)) {
		lua_pushcfunction(L, bytes_gc);
		lua_setfield(L, -2, "__gc");

		lua_pushcfunction(L, bytes_len);
		lua_setfield(L, -2, "__len");

		lua_pushcfunction(L, bytes_tostring_meta);
		lua_setfield(L, -2, "__tostring");
	}

	lua_pop(L, 1);

	luaL_newlib(L, byteslib);
	return 1;
}

void byteslib_register(lua_State *L) {
	preload_register(L, "termlet.bytes", luaopen_byteslib);
}
