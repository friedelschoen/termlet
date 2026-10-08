#include "corelib.h"

#include <errno.h>
#include <lauxlib.h>
#include <libutf8.h>
#include <lua.h>
#include <window/layout.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app);


struct lua_callback {
	lua_State *L;
	int ref;
};

struct win_layout layout;
static struct lua_callback layout_cbdata[CONFIG_WINDOW_LAYOUT_MAX_WINDOWS];
static struct lua_callback render_cbdata[CONFIG_WINDOW_LAYOUT_MAX_WINDOWS];

static int win_newmain(lua_State *L) {
	int z_index = luaL_checkinteger(L, 1);
	if (z_index < 0 || z_index > 255)
		return luaL_argerror(L, 1, "invalid z-index range");

	int handle = win_layout_new_main(&layout, z_index);
	if (handle < 0)
		return luaL_error(L, "cannot create window: %s", strerror(-handle));

	lua_pushinteger(L, handle);
	return 1;
}

static int win_newdialog(lua_State *L) {
	int z_index = luaL_checkinteger(L, 1);
	if (z_index < 0 || z_index > 255)
		return luaL_argerror(L, 1, "invalid z-index range");
	int width = luaL_checkinteger(L, 2);
	int height = luaL_checkinteger(L, 3);

	int handle = win_layout_new_dialog(&layout, z_index, width, height);
	if (handle < 0)
		return luaL_error(L, "cannot create window: %s", strerror(-handle));

	lua_pushinteger(L, handle);
	return 1;
}

static int win_newedge(lua_State *L, int (*new)(struct win_layout *, uint8_t, enum win_edge, int)) {
	int z_index = luaL_checkinteger(L, 1);
	if (z_index < 0 || z_index > 255)
		return luaL_argerror(L, 1, "invalid z-index range");
	const char *edgename = luaL_checkstring(L, 2);
	int size = luaL_checkinteger(L, 3);

	enum win_edge edge;
	if (strcmp(edgename, "left") == 0) {
		edge = WIN_EDGE_LEFT;
	} else if (strcmp(edgename, "right") == 0) {
		edge = WIN_EDGE_RIGHT;
	} else if (strcmp(edgename, "top") == 0) {
		edge = WIN_EDGE_TOP;
	} else if (strcmp(edgename, "bottom") == 0) {
		edge = WIN_EDGE_BOTTOM;
	} else {
		return luaL_argerror(L, 4, "invalid edge");
	}

	int handle = new(&layout, z_index, edge, size);
	if (handle < 0)
		return luaL_error(L, "cannot create window: %s", strerror(-handle));

	lua_pushinteger(L, handle);
	return 1;
}

static int win_newclip(lua_State *L) {
	return win_newedge(L, win_layout_new_clip);
}

static int win_newoverlay(lua_State *L) {
	return win_newedge(L, win_layout_new_overlay);
}

static int win_remove(lua_State *L) {
	int win = luaL_checkinteger(L, 1);

	win_layout_remove(&layout, win);
	return 0;
}

static void lua_layout_handler(struct win_layout *l, int win,
                               uint16_t cols, uint16_t rows,
                               bool visible, void *userdata) {
	ARG_UNUSED(l);
	ARG_UNUSED(win);
	LOG_INF("??");

	struct lua_callback *cb = userdata;
	lua_State *L = cb->L;

	lua_rawgeti(L, LUA_REGISTRYINDEX, cb->ref);

	/* push args */
	lua_pushinteger(L, cols);
	lua_pushinteger(L, rows);
	lua_pushboolean(L, visible);

	if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
		LOG_ERR("layout callback: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static int win_setlayouthandler(lua_State *L) {
	int win = luaL_checkinteger(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);

	if (win < 0 || win >= CONFIG_WINDOW_LAYOUT_MAX_WINDOWS)
		return luaL_argerror(L, 1, "invalid window handle");

	struct lua_callback *cb = &layout_cbdata[win];

	if (cb->ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, cb->ref);

	lua_pushvalue(L, 2); /* function */
	cb->ref = luaL_ref(L, LUA_REGISTRYINDEX);
	cb->L = L;

	win_layout_set_layout(
	    &layout,
	    win,
	    lua_layout_handler,
	    cb);

	return 0;
}


static void push_rect(lua_State *L, struct win_rect r) {
	lua_createtable(L, 0, 4);

	lua_pushinteger(L, r.x0);
	lua_setfield(L, -2, "x0");

	lua_pushinteger(L, r.x1);
	lua_setfield(L, -2, "x1");

	lua_pushinteger(L, r.y0);
	lua_setfield(L, -2, "y0");

	lua_pushinteger(L, r.y1);
	lua_setfield(L, -2, "y1");
}

static void lua_render_handler(struct win_layout *l, int win,
                               struct win_rect rect, void *userdata) {
	ARG_UNUSED(l);
	ARG_UNUSED(win);

	struct lua_callback *cb = userdata;
	lua_State *L = cb->L;

	lua_rawgeti(L, LUA_REGISTRYINDEX, cb->ref);
	push_rect(L, rect);

	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		LOG_ERR("render callback: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static int win_setrenderhandler(lua_State *L) {
	int win = luaL_checkinteger(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);

	if (win < 0 || win >= CONFIG_WINDOW_LAYOUT_MAX_WINDOWS)
		return luaL_argerror(L, 1, "invalid window handle");

	struct lua_callback *cb = &render_cbdata[win];

	if (cb->ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, cb->ref);

	lua_pushvalue(L, 2);
	cb->ref = luaL_ref(L, LUA_REGISTRYINDEX);
	cb->L = L;

	win_layout_set_render(
	    &layout,
	    win,
	    lua_render_handler,
	    cb);

	return 0;
}

static int win_enable(lua_State *L) {
	int win = luaL_checkinteger(L, 1);
	if (win < 0 || win >= CONFIG_WINDOW_LAYOUT_MAX_WINDOWS)
		return luaL_argerror(L, 1, "invalid window handle");
	if (lua_type(L, 2) != LUA_TBOOLEAN)
		return luaL_argerror(L, 2, "expected boolean");
	bool en = lua_toboolean(L, 2);
	win_layout_enable(&layout, win, en);
	return 0;
}

static int win_resize(lua_State *L) {
	int win = luaL_checkinteger(L, 1);
	if (win < 0 || win >= CONFIG_WINDOW_LAYOUT_MAX_WINDOWS)
		return luaL_argerror(L, 1, "invalid window handle");
	int width = luaL_checkinteger(L, 2);
	int height = luaL_checkinteger(L, 3);

	win_layout_resize(&layout, win, width, height);
	return 0;
}

static uint16_t rgb888_to_rgb565(uint32_t rgb) {
	uint8_t r = (rgb >> 16) & 0xff;
	uint8_t g = (rgb >> 8) & 0xff;
	uint8_t b = rgb & 0xff;

	return ((uint16_t) (r >> 3) << 11) |
	       ((uint16_t) (g >> 2) << 5) |
	       ((uint16_t) (b >> 3));
}

static int win_drawchar(lua_State *L) {
	int win = luaL_checkinteger(L, 1);
	if (win < 0 || win >= CONFIG_WINDOW_LAYOUT_MAX_WINDOWS)
		return luaL_argerror(L, 1, "invalid window handle");

	int x = luaL_checkinteger(L, 2);
	int y = luaL_checkinteger(L, 3);

	luaL_checktype(L, 4, LUA_TTABLE);

	struct win_char ch = {
		.code = ' ',
		.bg = 0x0000,
		.fg = 0xffff,
		.attr = 0,
	};

	/* char */
	if (lua_getfield(L, 4, "char") != LUA_TNIL) {
		int type = lua_type(L, -1);

		if (type == LUA_TSTRING) {
			size_t len;
			const char *s = lua_tolstring(L, -1, &len);

			uint32_t cp;
			int n = utf8_decode(s, strlen(s), &cp);

			if (n < 0 || (size_t) n != len) {
				lua_pop(L, 1);
				return luaL_argerror(
				    L, 5,
				    "char must contain exactly one UTF-8 character");
			}

			ch.code = cp;
		} else if (type == LUA_TNUMBER && lua_isinteger(L, -1)) {
			lua_Integer cp = lua_tointeger(L, -1);

			if (cp < 0 || cp > 0x10ffff ||
			    (cp >= 0xd800 && cp <= 0xdfff)) {
				lua_pop(L, 1);
				return luaL_argerror(
				    L, 5,
				    "char integer must be a valid Unicode codepoint");
			}

			ch.code = (uint32_t) cp;
		} else {
			lua_pop(L, 1);
			return luaL_argerror(
			    L, 4,
			    "char must be a UTF-8 string or integer codepoint");
		}
	}
	lua_pop(L, 1);

	/* foreground */
	if (lua_getfield(L, 4, "fg") != LUA_TNIL) {
		lua_Integer fg = luaL_checkinteger(L, -1);

		if (fg < 0 || fg > 0xffffff) {
			lua_pop(L, 1);
			return luaL_argerror(L, 4, "fg must be a 32-bit color");
		}

		ch.fg = rgb888_to_rgb565((uint32_t) fg);
	}
	lua_pop(L, 1);

	/* background */
	if (lua_getfield(L, 4, "bg") != LUA_TNIL) {
		lua_Integer bg = luaL_checkinteger(L, -1);

		if (bg < 0 || bg > 0xffffff) {
			lua_pop(L, 1);
			return luaL_argerror(L, 4, "bg must be a 32-bit color");
		}

		ch.bg = rgb888_to_rgb565((uint32_t) bg);
	}
	lua_pop(L, 1);

	/* attributes */
	if (lua_getfield(L, 4, "bold") != LUA_TNIL) {
		if (lua_type(L, -1) != LUA_TBOOLEAN) {
			lua_pop(L, 1);
			return luaL_argerror(L, 4, "bold must be a boolean");
		}

		if (lua_toboolean(L, -1))
			ch.attr |= WIN_CHAR_BOLD;
	}
	lua_pop(L, 1);

	if (lua_getfield(L, 4, "italic") != LUA_TNIL) {
		if (lua_type(L, -1) != LUA_TBOOLEAN) {
			lua_pop(L, 1);
			return luaL_argerror(L, 4, "italic must be a boolean");
		}

		if (lua_toboolean(L, -1))
			ch.attr |= WIN_CHAR_ITALIC;
	}
	lua_pop(L, 1);

	win_layout_draw_char(&layout, win, x, y, ch);
	return 0;
}

static const luaL_Reg windowlib[] = {
	{ "newmain", win_newmain },
	{ "newdialog", win_newdialog },
	{ "newclip", win_newclip },
	{ "newoverlay", win_newoverlay },
	{ "remove", win_remove },
	{ "setlayouthandler", win_setlayouthandler },
	{ "setrenderhandler", win_setrenderhandler },
	{ "enable", win_enable },
	{ "resize", win_resize },
	{ "drawchar", win_drawchar },
	{ 0 }
};

static int luaopen_windowlib(lua_State *L) {
	for (int i = 0; i < CONFIG_WINDOW_LAYOUT_MAX_WINDOWS; i++) {
		layout_cbdata[i].ref = LUA_NOREF;
		render_cbdata[i].ref = LUA_NOREF;
	}

	luaL_newlib(L, windowlib);
	return 1;
}

void luaclose_windowlib(lua_State *L) {
	for (int i = 0; i < CONFIG_WINDOW_LAYOUT_MAX_WINDOWS; i++) {
		layout_cbdata[i].L = NULL;
		if (layout_cbdata[i].ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, layout_cbdata[i].ref);
			layout_cbdata[i].ref = LUA_NOREF;
		}

		render_cbdata[i].L = NULL;
		if (render_cbdata[i].ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, render_cbdata[i].ref);
			render_cbdata[i].ref = LUA_NOREF;
		}
	}
}

void windowlib_register(lua_State *L) {
	preload_register(L, "termlet._core.window", luaopen_windowlib);
}
