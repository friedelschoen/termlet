#include "sysfs.h"

#include "zephyr/fs/fs_interface.h"
#include "zephyr/sys/printk.h"
#include "zephyr/toolchain.h"

#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_sys.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>


/***** HELPERS *****/

static int sysfs_findentry(struct sysfs_fs *fs, const char *path, const struct sysfs_entry **result) {
	char name[MAX_FILE_NAME + 1];
	const char *end;

	const struct sysfs_entry *en = *result ? *result : fs->root;
	if (path[0] == '/') {
		en = fs->root;
		path++;
	}

	do {
		end = strchr(path, '/');
		if (end) {
			if ((size_t) (end - path) >= sizeof(name))
				return -ENAMETOOLONG;

			memcpy(name, path, end - path);
			name[end - path] = '\0';
			path = end + 1;
		} else {
			if (strlen(path) >= sizeof(name))
				return -ENAMETOOLONG;

			strncpy(name, path, sizeof(name));
		}

		if (strcmp(name, "") == 0 || strcmp(name, ".") == 0)
			continue;

		if (strcmp(name, "..") == 0) {
			if (en->parent)
				en = en->parent;
			continue;
		}

		if (en->type != FS_DIR_ENTRY_DIR)
			return -ENOTDIR;

		const struct sysfs_entry *next = NULL;

		for (int i = 0; i < en->size; i++) {
			if (strcmp(en->entries[i]->name, name) == 0) {
				next = en->entries[i];
				break;
			}
		}
		if (next == NULL)
			return -ENOENT;
		en = next;
	} while (end);

	*result = en;
	return 0;
}

static void sysfs_copyentry(struct fs_dirent *dest, const struct sysfs_entry *src) {
	dest->type = src->type;
	dest->size = src->type == FS_DIR_ENTRY_FILE ? src->size : 0;
	memcpy(dest->name, src->name, MIN(sizeof(dest->name), sizeof(src->name)));
}

static const char *sysfs_stripmnt(const struct fs_mount_t *mnt, const char *path) {
	size_t len = strlen(mnt->mnt_point);

	if (strncmp(path, mnt->mnt_point, len) != 0)
		return path;

	path += len;

	if (*path == '\0')
		return "/";

	return path;
}

/***** GENERICS *****/

static int sysfs_openfd(struct sysfs_fs *fs, const char *path, struct sysfs_fd **result) {
	int ret;

	if (result == NULL || *result != NULL || path == NULL)
		return -EINVAL;

	const struct sysfs_entry *entry = NULL;
	if ((ret = sysfs_findentry(fs, path, &entry)) != 0)
		return ret;

	for (int i = 0; i < (int) ARRAY_SIZE(fs->fds); i++) {
		struct sysfs_fd *fd = &fs->fds[i];
		if (fd->used)
			continue;

		fd->used = true;
		fd->entry = entry;
		fd->offset = 0;
		*result = fd;
		return 0;
	}

	return -ENFILE;
}

static int sysfs_closefd(struct sysfs_fd *ptr) {
	if (ptr == NULL)
		return -EIO;

	memset(ptr, 0, sizeof(*ptr));
	return 0;
}

/***** IMPLEMENT *****/


static int sysfs_open(struct fs_file_t *zfp, const char *path, fs_mode_t flags) {
	struct sysfs_fs *fs;
	if (zfp == NULL || zfp->filep != NULL || path == NULL)
		return -EINVAL;

	/* only reading is allowed */
	if (flags != FS_O_READ)
		return -EINVAL;

	fs = zfp->mp->fs_data;
	path = sysfs_stripmnt(zfp->mp, path);
	return sysfs_openfd(fs, path, (void *) &zfp->filep);
}


static int sysfs_close(struct fs_file_t *zfp) {
	if (zfp == NULL)
		return -EINVAL;

	return sysfs_closefd(zfp->filep);
}

static int sysfs_unlink(struct fs_mount_t *mountp, const char *path) {
	ARG_UNUSED(mountp);
	ARG_UNUSED(path);
	return -EROFS;
}

static int sysfs_rename(struct fs_mount_t *mountp, const char *from, const char *to) {
	ARG_UNUSED(mountp);
	ARG_UNUSED(from);
	ARG_UNUSED(to);
	return -EROFS;
}

static ssize_t sysfs_read(struct fs_file_t *zfp, void *ptr, size_t size) {
	if (zfp == NULL || zfp->filep == NULL)
		return -EINVAL;

	struct sysfs_fd *fd = zfp->filep;
	if (!fd->used)
		return -EBADFD;

	if (fd->entry->type != FS_DIR_ENTRY_FILE)
		return -EISDIR;

	size_t n = MIN((off_t) size, fd->entry->size - fd->offset);
	memcpy(ptr, fd->entry->content + fd->offset, n);
	fd->offset += n;

	return n;
}

static ssize_t sysfs_write(struct fs_file_t *zfp, const void *ptr, size_t size) {
	ARG_UNUSED(zfp);
	ARG_UNUSED(ptr);
	ARG_UNUSED(size);
	return -EROFS;
}

static int sysfs_seek(struct fs_file_t *zfp, off_t offset, int whence) {
	if (zfp == NULL || zfp->filep == NULL) {
		return -EINVAL;
	}

	struct sysfs_fd *fd = zfp->filep;
	off_t cur = 0;
	switch (whence) {
		case FS_SEEK_SET:
			cur = offset;
			break;
		case FS_SEEK_CUR:
			cur = fd->offset + offset;
			break;
		case FS_SEEK_END:
			cur = fd->entry->size + offset;
			break;
		default:
			return -EINVAL;
	}

	if (cur < 0 || cur > fd->entry->size)
		return -EINVAL;
	fd->offset = cur;

	return 0;
}

static off_t sysfs_tell(struct fs_file_t *zfp) {
	if (zfp == NULL || zfp->filep == NULL) {
		return -EINVAL;
	}

	struct sysfs_fd *fd = zfp->filep;
	return fd->offset;
}

static int sysfs_truncate(struct fs_file_t *zfp, off_t length) {
	ARG_UNUSED(zfp);
	ARG_UNUSED(length);
	return -EROFS;
}

static int sysfs_sync(struct fs_file_t *zfp) {
	ARG_UNUSED(zfp);
	return 0;
}

static int sysfs_mkdir(struct fs_mount_t *mountp, const char *path) {
	ARG_UNUSED(mountp);
	ARG_UNUSED(path);
	return -EROFS;
}

static int sysfs_opendir(struct fs_dir_t *zfp, const char *path) {
	struct sysfs_fs *fs;
	if (zfp == NULL || zfp->dirp != NULL || path == NULL)
		return -EINVAL;

	fs = zfp->mp->fs_data;
	path = sysfs_stripmnt(zfp->mp, path);
	return sysfs_openfd(fs, path, (void *) &zfp->dirp);
}

static int sysfs_readdir(struct fs_dir_t *zdp, struct fs_dirent *entry) {
	if (zdp == NULL || zdp->dirp == NULL)
		return -EINVAL;

	struct sysfs_fd *fd = zdp->dirp;
	if (!fd->used)
		return -EBADFD;

	if (fd->entry->type != FS_DIR_ENTRY_DIR)
		return -ENOTDIR;

	if (fd->offset >= fd->entry->size) {
		entry->name[0] = '\0';
		return 0;
	}

	sysfs_copyentry(entry, fd->entry->entries[fd->offset++]);
	return 0;
}

static int sysfs_closedir(struct fs_dir_t *zdp) {
	if (zdp == NULL)
		return -EINVAL;

	return sysfs_closefd(zdp->dirp);
}

static int sysfs_stat(struct fs_mount_t *mountp, const char *path, struct fs_dirent *entry) {
	if (mountp == NULL || path == NULL || entry == NULL)
		return -EINVAL;

	struct sysfs_fs *fs = mountp->fs_data;
	path = sysfs_stripmnt(mountp, path);
	const struct sysfs_entry *result = NULL;
	int ret = 0;

	if ((ret = sysfs_findentry(fs, path, &result)) != 0)
		return ret;

	sysfs_copyentry(entry, result);
	return 0;
}

static int sysfs_statvfs(struct fs_mount_t *mountp, const char *path, struct fs_statvfs *stat) {
	ARG_UNUSED(mountp);
	ARG_UNUSED(path);
	ARG_UNUSED(stat);
	return -ENOTSUP;
}

static int sysfs_mount(struct fs_mount_t *mountp) {
	ARG_UNUSED(mountp);
	return 0;
}

static int sysfs_unmount(struct fs_mount_t *mountp) {
	ARG_UNUSED(mountp);
	return 0;
}

/* File system interface */
struct fs_file_system_t sysfs_api = {
	.open = sysfs_open,
	.close = sysfs_close,
	.read = sysfs_read,
	.write = sysfs_write,
	.lseek = sysfs_seek,
	.tell = sysfs_tell,
	.truncate = sysfs_truncate,
	.sync = sysfs_sync,
	.opendir = sysfs_opendir,
	.readdir = sysfs_readdir,
	.closedir = sysfs_closedir,
	.mount = sysfs_mount,
	.unmount = sysfs_unmount,
	.unlink = sysfs_unlink,
	.rename = sysfs_rename,
	.mkdir = sysfs_mkdir,
	.stat = sysfs_stat,
	.statvfs = sysfs_statvfs,
};

int sysfs_register() {
	return fs_register(FS_SYSFS, &sysfs_api);
}

SYS_INIT(sysfs_register, POST_KERNEL, CONFIG_SYSFS_INIT_PRIORITY);
