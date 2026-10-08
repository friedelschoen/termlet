#include "corelib/corelib.h"
#include "internal.h"
#include "window/layout.h"

#include <devfs/devfs.h>
#include <lauxlib.h>
#include <lualib.h>
#include <sysfs/sysfs.h>
#include <termlet_fs.h>
#include <zephyr/drivers/display.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
LOG_MODULE_REGISTER(app);

#define HAS_FLAGS(value, flags) (((value) & (flags)) == (flags))

const struct font *regular_font;
const struct font *bold_font;
const struct font *italic_font;
const struct font *bolditalic_font;

extern const struct devfs_entry devices[];
extern const struct sysfs_entry sysfs_root;

static struct sysfs_fs sysfs_data = {
	.root = &sysfs_root,
};

struct fs_mount_t mountpoints[] = {
	{ .type = FS_SYSFS, .mnt_point = "/sys", .fs_data = &sysfs_data },
	{ .type = FS_DEVFS, .mnt_point = "/dev", .fs_data = (void *) devices }
};

K_HEAP_DEFINE(lua_heap, CONFIG_LUA_HEAP_SIZE);

static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
	struct k_heap *heap = ud;

	ARG_UNUSED(osize);

	if (nsize == 0) {
		k_heap_free(heap, ptr);
		return NULL;
	}

	return k_heap_realloc(heap, ptr, nsize, K_NO_WAIT);
}

static int walk_tree(const char *path, unsigned depth) {
	struct fs_dir_t dir;
	struct fs_dirent entry;
	char child[256];
	int ret;

	fs_dir_t_init(&dir);

	ret = fs_opendir(&dir, path);
	if (ret < 0) {
		LOG_ERR("opendir(%s): %d", path, ret);
		return ret;
	}

	for (;;) {
		ret = fs_readdir(&dir, &entry);
		if (ret < 0) {
			LOG_ERR("readdir(%s): %d", path, ret);
			break;
		}

		if (entry.name[0] == '\0')
			break;

		LOG_INF("%*s%s%s %d",
		        (int) (depth * 2), "",
		        entry.name,
		        entry.type == FS_DIR_ENTRY_DIR ? "/" : "",
		        entry.size);

		ret = snprintf(child, sizeof(child),
		               "%s%s%s",
		               path,
		               path[strlen(path) - 1] == '/' ? "" : "/",
		               entry.name);

		if (ret < 0 || ret >= (int) sizeof(child)) {
			LOG_ERR("path too long: %s/%s",
			        path, entry.name);
			ret = -ENAMETOOLONG;
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR) {
			ret = walk_tree(child, depth + 1);
			if (ret < 0)
				break;
		}
	}

	{
		int close_ret = fs_closedir(&dir);

		if (ret == 0 && close_ret < 0)
			ret = close_ret;
	}

	return ret;
}

void init_mount() {
	LOG_INF("mount points");
	int ret;
	for (int i = 0; i < (int) ARRAY_SIZE(mountpoints); i++) {
		if ((ret = fs_mount(&mountpoints[i])) != 0) {
			LOG_ERR("unable to mount %s: %d %s", mountpoints[i].mnt_point, ret, strerror(-ret));
		}
	}

	walk_tree("/", 0);
}

static int openlibs(lua_State *L) {
	luaL_openselectedlibs(
	    L,
	    LUA_GLIBK |
	        LUA_LOADLIBK |
	        LUA_STRLIBK |
	        LUA_UTF8LIBK |
	        LUA_TABLIBK |
	        LUA_MATHLIBK |
	        LUA_IOLIBK,
	    0);

	corelib_register(L);

	return 0;
}

const struct font *get_font(const char *name, const char *style, int size, const struct font *fallback) {
	for (size_t i = 0; i < font_count; i++) {
		if (strcmp(fonts[i]->name, name) == 0 && strcmp(fonts[i]->style, style) == 0 && fonts[i]->size == size) {
			return fonts[i];
		}
	}
	return fallback;
}

void init_fonts() {
	regular_font = get_font("Go Mono", "Regular", 14, NULL);
	bold_font = get_font("Go Mono", "Bold", 14, regular_font);
	italic_font = get_font("Go Mono", "Italic", 14, bold_font);
	bolditalic_font = get_font("Go Mono", "Bold Italic", 14, italic_font);
}

lua_State *init_lua() {
	lua_State *L = lua_newstate(lua_alloc, &lua_heap, 42);
	if (L == NULL) {
		LOG_ERR("cannot create Lua state");
		return NULL;
	}

	lua_pushcfunction(L, openlibs);
	int status = lua_pcall(L, 0, 0, 0);
	if (status != LUA_OK) {
		LOG_ERR("cannot initialize Lua: %s", lua_tostring(L, -1));
		lua_close(L);
		return NULL;
	}

	status = luaL_dofile(L, "/sys/init.lua");
	if (status != LUA_OK) {
		LOG_ERR("lua error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
	}

	LOG_INF("Lua initalized");
	return L;
}

void draw_char(uint16_t x, uint16_t y, struct win_char ch, void *userdata) {
	struct renderer *r = userdata;
	const struct font *font = regular_font;
	if (HAS_FLAGS(ch.attr, WIN_CHAR_BOLD | WIN_CHAR_ITALIC)) {
		font = bolditalic_font;
	} else if (HAS_FLAGS(ch.attr, WIN_CHAR_BOLD)) {
		font = bold_font;
	} else if (HAS_FLAGS(ch.attr, WIN_CHAR_ITALIC)) {
		font = italic_font;
	}
	draw_glyph(r, ch.code, x, y, font, ch.bg, ch.fg);
}

void commit(void *userdata) {
	struct renderer *r = userdata;
	draw_commit(r);
}

int main() {
	init_mount();
	init_fonts();

	struct renderer renderer = { 0 };
	renderer.columns = DISPLAY_WIDTH / regular_font->width;
	renderer.rows = DISPLAY_HEIGHT / regular_font->height;
	renderer.display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));


	int ret = display_set_pixel_format(renderer.display, PIXEL_FORMAT_RGB_565);
	if (ret < 0) {
		LOG_INF("Unable to select RGB565: %d", ret);
		return 0;
	}
	ret = display_blanking_off(renderer.display);
	if (ret < 0)
		LOG_INF("display_blanking_off failed: %d", ret);

	memset(&layout, 0, sizeof(layout));
	layout.bounds.x1 = renderer.columns;
	layout.bounds.y1 = renderer.rows;
	layout.draw_char = draw_char;
	layout.commit = commit;
	layout.userdata = &renderer;

	lua_State *L = init_lua();

#define FPS      60
#define FRAME_US (1000000 / FPS)

	int64_t next = k_uptime_get() * 1000;

	for (;;) {
		win_layout_render(&layout);

		next += FRAME_US;

		int64_t now = k_uptime_get() * 1000;
		if (next > now)
			k_usleep(next - now);
	}

	lua_close(L);
	return 0;
}
