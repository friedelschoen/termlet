#include "zephyr/fs/fs_interface.h"

#include <lauxlib.h>
#include <lualib.h>
#include <sysfs.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
LOG_MODULE_REGISTER(app);

extern const struct sysfs_entry sysfs_root;

static struct sysfs_fs sysfs_data = {
	.root = &sysfs_root,
};

struct fs_mount_t mountpoints[] = {
	{ .type = FS_SYSFS, .mnt_point = "/sys", .fs_data = &sysfs_data }
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

void test() {
	FILE *f = fopen("/sys/init.lua", "r");
	if (!f) {
		perror("fopen");
		return;
	}

	int c;
	while ((c = getc(f)) != EOF)
		printk("%02x ", (unsigned) c);

	printk("\n");
	fclose(f);
}

int main() {
	int ret;

	LOG_INF("mount points");
	for (int i = 0; i < (int) ARRAY_SIZE(mountpoints); i++)
		if ((ret = fs_mount(&mountpoints[i])) != 0) {
			LOG_ERR("unable to mount %s: %d %s", mountpoints[i].mnt_point, ret, strerror(-ret));
		}

	walk_tree("/", 0);

//	test();

	lua_State *L = lua_newstate(lua_alloc, &lua_heap, 42);

	/*
	[x] LUA_GLIBK : the basic library.
	[x] LUA_LOADLIBK : the package library.
	[ ] LUA_COLIBK : the coroutine library.
	[x] LUA_STRLIBK : the string library.
	[x] LUA_UTF8LIBK : the UTF-8 library.
	[x] LUA_TABLIBK : the table library.
	[x] LUA_MATHLIBK : the mathematical library.
	[x] LUA_IOLIBK : the I/O library.
	[ ] ~LUA_OSLIBK~ : the operating system library. disabled.
	[ ] LUA_DBLIBK : the debug library.
	*/
	luaL_openselectedlibs(L, LUA_GLIBK | LUA_LOADLIBK | LUA_IOLIBK | LUA_STRLIBK | LUA_UTF8LIBK | LUA_TABLIBK | LUA_MATHLIBK, 0);

	if (luaL_dofile(L, "/sys/init.lua") != LUA_OK)
		LOG_ERR("lua error: %s", lua_tostring(L, -1));

	lua_close(L);

	LOG_INF("done");
	return 0;
}
