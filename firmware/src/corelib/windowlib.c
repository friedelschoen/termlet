#include "callback.h"
#include "corelib.h"
#include "window/input.h"

#include <ctype.h>
#include <errno.h>
#include <lauxlib.h>
#include <libutf8.h>
#include <lua.h>
#include <window/layout.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app);

struct colorname {
	const char *name;
	uint16_t rgb565;
	uint32_t rgb888;
};

static const struct colorname colors[16] = {
	{ "black", 0x0000, 0x000000 },
	{ "red", 0xc800, 0xcd0000 },
	{ "green", 0x0660, 0x00cd00 },
	{ "yellow", 0xce60, 0xcdcd00 },
	{ "blue", 0x001d, 0x0000ee },
	{ "magenta", 0xc819, 0xcd00cd },
	{ "cyan", 0x0679, 0x00cdcd },
	{ "white", 0xe73c, 0xe5e5e5 },

	{ "bright_black", 0x7bef, 0x7f7f7f },
	{ "bright_red", 0xf800, 0xff0000 },
	{ "bright_green", 0x07e0, 0x00ff00 },
	{ "bright_yellow", 0xffe0, 0xffff00 },
	{ "bright_blue", 0x5aff, 0x5c5cff },
	{ "bright_magenta", 0xf81f, 0xff00ff },
	{ "bright_cyan", 0x07ff, 0x00ffff },
	{ "bright_white", 0xffff, 0xffffff },
};

struct win_layout layout;
static struct lua_callback layout_cbdata[CONFIG_WINDOW_LAYOUT_MAX_WINDOWS];
static struct lua_callback render_cbdata[CONFIG_WINDOW_LAYOUT_MAX_WINDOWS];
static struct lua_callback input_cbdata[CONFIG_WINDOW_LAYOUT_MAX_WINDOWS];

/* == ARBINARY TYPE HELPERS */

static void lua_pushrect(lua_State *L, struct win_rect r) {
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

static int hexchar(char chr) {
	if (chr >= '0' && chr <= '9')
		return chr - '0';
	if (chr >= 'a' && chr <= 'f')
		return chr - 'a' + 10;
	if (chr >= 'A' && chr <= 'F')
		return chr - 'A' + 10;
	return -1;
}

static uint16_t rgb888_to_rgb565(uint32_t rgb) {
	uint16_t r = (rgb >> 16) & 0xff;
	uint16_t g = (rgb >> 8) & 0xff;
	uint16_t b = rgb & 0xff;

	return ((r >> 3) << 11) |
	       ((g >> 2) << 5) |
	       (b >> 3);
}

static uint16_t lua_colorto565(lua_State *L, int arg) {
	if (lua_type(L, arg) == LUA_TNUMBER) {
		lua_Integer rgb = luaL_checkinteger(L, arg);

		if (rgb < 0 || rgb > 0xffffff)
			return luaL_argerror(L, arg, "color out of range");

		return rgb888_to_rgb565((uint32_t) rgb);
	}

	if (lua_type(L, arg) == LUA_TSTRING) {
		const char *name = lua_tostring(L, arg);

		if (name[0] == '#') {
			name++;

			int r, g, b;

			switch (strlen(name)) {
				case 3:
					r = hexchar(name[0]);
					g = hexchar(name[1]);
					b = hexchar(name[2]);

					if (r < 0 || g < 0 || b < 0)
						return luaL_argerror(
						    L, arg, "invalid color representation");

					r = (r << 4) | r;
					g = (g << 4) | g;
					b = (b << 4) | b;
					break;

				case 6: {
					int rh = hexchar(name[0]);
					int rl = hexchar(name[1]);
					int gh = hexchar(name[2]);
					int gl = hexchar(name[3]);
					int bh = hexchar(name[4]);
					int bl = hexchar(name[5]);

					if (rh < 0 || rl < 0 ||
					    gh < 0 || gl < 0 ||
					    bh < 0 || bl < 0)
						return luaL_argerror(
						    L, arg, "invalid color representation");

					r = (rh << 4) | rl;
					g = (gh << 4) | gl;
					b = (bh << 4) | bl;
					break;
				}

				default:
					return luaL_argerror(
					    L, arg, "invalid color representation");
			}

			uint32_t rgb =
			    ((uint32_t) r << 16) |
			    ((uint32_t) g << 8) |
			    (uint32_t) b;

			return rgb888_to_rgb565(rgb);
		}

		for (int i = 0; i < (int) ARRAY_SIZE(colors); i++) {
			if (strcmp(name, colors[i].name) == 0)
				return colors[i].rgb565;
		}

		return luaL_argerror(L, arg, "invalid color name");
	}

	return luaL_argerror(
	    L, arg, "invalid color: expected number or string");
}

/* == FUNC HELPERS == */

static struct win_state *lua_checkwindow(lua_State *L, int idx) {
	struct win_state *w = lua_touserdata(L, idx);

	if (!w || !w->used)
		luaL_argerror(L, idx, "invalid window handle");

	return w;
}

/* == IMPLEMENTATION == */

static int win_new(lua_State *L) {
	struct win_state *w = win_layout_new(&layout);
	if (!w)
		return luaL_error(L, "cannot create window");

	lua_pushlightuserdata(L, w);
	return 1;
}

static int win_remove(lua_State *L) {
	struct win_state *w = lua_checkwindow(L, 1);

	win_layout_remove(w);
	return 0;
}

static int win_get(lua_State *L) {
	struct win_state *w = lua_checkwindow(L, 1);
	const char *key = luaL_checkstring(L, 2);

	if (strcmp(key, "enabled") == 0) {
		lua_pushboolean(L, w->enabled);
		return 1;
	}

	if (strcmp(key, "focusable") == 0) {
		lua_pushboolean(L, w->focusable);
		return 1;
	}

	if (strcmp(key, "mode") == 0) {
		switch (w->mode) {
			case WIN_MODE_MAIN:
				lua_pushliteral(L, "main");
				return 1;
			case WIN_MODE_CLIP:
				lua_pushliteral(L, "clip");
				return 1;
			case WIN_MODE_OVERLAY:
				lua_pushliteral(L, "overlay");
				return 1;
			case WIN_MODE_DIALOG:
				lua_pushliteral(L, "dialog");
				return 1;
			default:
				return 0;
		}
	}


	if (strcmp(key, "edge") == 0) {
		switch (w->edge) {
			case WIN_EDGE_TOP:
				lua_pushliteral(L, "top");
				return 1;
			case WIN_EDGE_BOTTOM:
				lua_pushliteral(L, "bottom");
				return 1;
			case WIN_EDGE_LEFT:
				lua_pushliteral(L, "left");
				return 1;
			case WIN_EDGE_RIGHT:
				lua_pushliteral(L, "right");
				return 1;
			default:
				return 0;
		}
	}

	if (strcmp(key, "z_index") == 0) {
		lua_pushinteger(L, w->z_index);
		return 1;
	}

	if (strcmp(key, "width") == 0) {
		lua_pushinteger(L, w->width);
		return 1;
	}

	if (strcmp(key, "height") == 0) {
		lua_pushinteger(L, w->height);
		return 1;
	}

	if (strcmp(key, "visible") == 0) {
		lua_pushboolean(L, w->visible);
		return 1;
	}

	if (strcmp(key, "win_bounds") == 0) {
		lua_pushrect(L, w->win_bounds);
		return 1;
	}

	if (strcmp(key, "chars_bounds") == 0) {
		lua_pushrect(L, w->chars_bounds);
		return 1;
	}

	return luaL_error(L, "unknown window property '%s'", key);
}


static void lua_layout_handler(struct win_state *w,
                               uint16_t cols, uint16_t rows,
                               bool visible, void *userdata) {
	ARG_UNUSED(w);

	struct lua_callback *cb = userdata;
	lua_State *L = cb->L;

	lua_callback_push(cb);
	lua_pushinteger(L, cols);
	lua_pushinteger(L, rows);
	lua_pushboolean(L, visible);

	if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
		LOG_ERR("layout callback: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static void lua_render_handler(struct win_state *w,
                               struct win_rect rect, void *userdata) {
	ARG_UNUSED(w);

	struct lua_callback *cb = userdata;
	lua_State *L = cb->L;

	lua_callback_push(cb);
	lua_pushrect(L, rect);

	if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
		LOG_ERR("render callback: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

static void lua_input_handler(struct win_state *w, union win_input_event *ev, void *userdata) {
	ARG_UNUSED(w);

	struct lua_callback *cb = userdata;
	lua_State *L = cb->L;

	lua_callback_push(cb);
	/* TODO: push event */

	if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
		LOG_ERR("render callback: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}

#define win_index(w) ((w) - (w)->l->windows)

static int win_set(lua_State *L) {
	struct win_state *w = lua_checkwindow(L, 1);
	const char *key = luaL_checkstring(L, 2);

	if (strcmp(key, "enabled") == 0) {
		w->enabled = lua_toboolean(L, 3);
		return 0;
	}

	if (strcmp(key, "focusable") == 0) {
		w->focusable = lua_toboolean(L, 3);
		return 0;
	}

	if (strcmp(key, "mode") == 0) {
		const char *mode = luaL_checkstring(L, 3);
		if (strcmp(mode, "main") == 0) {
			w->mode = WIN_MODE_MAIN;
		} else if (strcmp(mode, "clip") == 0) {
			w->mode = WIN_MODE_CLIP;
		} else if (strcmp(mode, "overlay") == 0) {
			w->mode = WIN_MODE_OVERLAY;
		} else if (strcmp(mode, "dialog") == 0) {
			w->mode = WIN_MODE_DIALOG;
		} else {
			return luaL_error(L, "invalid window mode: %s", mode);
		}
		return 0;
	}

	if (strcmp(key, "edge") == 0) {
		const char *edge = luaL_checkstring(L, 3);
		if (strcmp(edge, "top") == 0) {
			w->edge = WIN_EDGE_TOP;
		} else if (strcmp(edge, "bottom") == 0) {
			w->edge = WIN_EDGE_BOTTOM;
		} else if (strcmp(edge, "left") == 0) {
			w->edge = WIN_EDGE_LEFT;
		} else if (strcmp(edge, "right") == 0) {
			w->edge = WIN_EDGE_RIGHT;
		} else {
			return luaL_error(L, "invalid window edge: %s", edge);
		}
		return 0;
	}

	if (strcmp(key, "z_index") == 0) {
		w->z_index = luaL_checkinteger(L, 3);
		return 0;
	}

	if (strcmp(key, "width") == 0) {
		w->width = luaL_checkinteger(L, 3);
		lua_pushinteger(L, w->width);
		return 0;
	}

	if (strcmp(key, "height") == 0) {
		w->height = luaL_checkinteger(L, 3);
		return 0;
	}

	if (strcmp(key, "layout") == 0) {
		luaL_checktype(L, 3, LUA_TFUNCTION);

		struct lua_callback *cb = &layout_cbdata[win_index(w)];
		lua_callback_set(L, cb, 3);

		w->layout = lua_layout_handler;
		w->layout_userdata = cb;
		return 0;
	}

	if (strcmp(key, "render") == 0) {
		luaL_checktype(L, 3, LUA_TFUNCTION);

		struct lua_callback *cb = &render_cbdata[win_index(w)];
		lua_callback_set(L, cb, 3);

		w->render = lua_render_handler;
		w->render_userdata = cb;
		return 0;
	}

	if (strcmp(key, "input") == 0) {
		luaL_checktype(L, 3, LUA_TFUNCTION);

		struct lua_callback *cb = &input_cbdata[win_index(w)];
		lua_callback_set(L, cb, 3);

		w->input = lua_input_handler;
		w->input_userdata = cb;
		return 0;
	}

	return luaL_error(L, "unknown or read-only window property '%s'", key);
}

static int win_drawchar(lua_State *L) {
	struct win_state *w = lua_checkwindow(L, 1);
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
		ch.fg = lua_colorto565(L, -1);
	}
	lua_pop(L, 1);

	/* background */
	if (lua_getfield(L, 4, "bg") != LUA_TNIL) {
		ch.bg = lua_colorto565(L, -1);
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

	win_layout_draw_char(w, x, y, ch);
	return 0;
}

static int win_update(lua_State *L) {
	struct win_state *w = lua_checkwindow(L, 1);

	win_layout_update(w->l);
	return 0;
}

static const luaL_Reg windowlib[] = {
	{ "new", win_new },
	{ "remove", win_remove },
	{ "get", win_get },
	{ "set", win_set },
	{ "drawchar", win_drawchar },
	{ "update", win_update },
	{ 0 }
};

static int luaopen_windowlib(lua_State *L) {
	for (int i = 0; i < CONFIG_WINDOW_LAYOUT_MAX_WINDOWS; i++) {
		lua_callback_init(&layout_cbdata[i]);
		lua_callback_init(&render_cbdata[i]);
		lua_callback_init(&input_cbdata[i]);
	}

	luaL_newlib(L, windowlib);
	return 1;
}

void luaclose_windowlib(lua_State *L) {
	for (int i = 0; i < CONFIG_WINDOW_LAYOUT_MAX_WINDOWS; i++) {
		lua_callback_clear(&layout_cbdata[i]);
		lua_callback_clear(&render_cbdata[i]);
		lua_callback_clear(&input_cbdata[i]);
	}
}

void windowlib_register(lua_State *L) {
	preload_register(L, "termlet._core.window", luaopen_windowlib);
}
