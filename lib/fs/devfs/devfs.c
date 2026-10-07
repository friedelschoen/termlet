#include "devfs.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <termlet_fs.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_sys.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>


struct devfs_fd {
	const struct devfs_entry *entry;
	off_t offset;
	void *priv;
};

struct devfs_dir {
	const struct devfs_entry *entries;
	size_t index;
};

/***** FILES *****/

static const char *devfs_stripmnt(const struct fs_mount_t *mnt,
                                  const char *path) {
	size_t len = strlen(mnt->mnt_point);

	if (strncmp(path, mnt->mnt_point, len) == 0)
		path += len;

	if (*path == '\0')
		return "/";

	return path;
}


static const struct devfs_entry *
devfs_find(const struct devfs_entry *entries, const char *path) {
	if (*path == '/')
		path++;

	/* devfs is deliberately flat. */
	if (*path == '\0' || strchr(path, '/') != NULL)
		return NULL;

	const struct devfs_entry *cur = entries;
	while (cur->name != NULL) {
		if (strcmp(cur->name, path) == 0)
			return cur;
		cur++;
	}

	return NULL;
}


static bool devfs_valid_name(const char *name) {
	if (name == NULL || *name == '\0')
		return false;

	/* Entries live directly below /dev. */
	return strchr(name, '/') == NULL;
}


/***** FILES *****/

static int devfs_open(struct fs_file_t *zfp,
                      const char *path,
                      fs_mode_t flags) {
	if (zfp == NULL || zfp->mp == NULL || path == NULL)
		return -EINVAL;

	if (zfp->filep != NULL)
		return -EBUSY;

	const struct devfs_entry *entries = zfp->mp->fs_data;
	if (entries == NULL)
		return -EINVAL;

	path = devfs_stripmnt(zfp->mp, path);

	const struct devfs_entry *entry = devfs_find(entries, path);
	if (entry == NULL)
		return -ENOENT;

	struct devfs_fd *fd = k_malloc(sizeof(*fd));
	if (fd == NULL)
		return -ENOMEM;

	*fd = (struct devfs_fd){
		.entry = entry,
		.offset = 0,
		.priv = NULL,
	};

	zfp->filep = fd;

	if (entry->ops != NULL && entry->ops->open != NULL) {
		int ret = entry->ops->open(entry, zfp, flags);

		if (ret < 0) {
			zfp->filep = NULL;
			k_free(fd);
			return ret;
		}
	}

	return 0;
}


static int devfs_close(struct fs_file_t *zfp) {
	if (zfp == NULL || zfp->filep == NULL)
		return -EBADF;

	struct devfs_fd *fd = zfp->filep;
	int ret = 0;

	if (fd->entry->ops != NULL &&
	    fd->entry->ops->close != NULL) {
		ret = fd->entry->ops->close(fd->entry, zfp);
	}

	if (ret < 0)
		return ret;

	zfp->filep = NULL;
	k_free(fd);

	return 0;
}


static ssize_t devfs_read(struct fs_file_t *zfp, void *buf, size_t size) {
	if (zfp == NULL || zfp->filep == NULL)
		return -EBADF;

	if (buf == NULL && size != 0)
		return -EINVAL;

	struct devfs_fd *fd = zfp->filep;

	if (fd->entry->ops == NULL ||
	    fd->entry->ops->read == NULL)
		return -ENOTSUP;

	ssize_t ret =
	    fd->entry->ops->read(fd->entry, zfp, buf, size);

	if (ret > 0)
		fd->offset += ret;

	return ret;
}


static ssize_t devfs_write(struct fs_file_t *zfp,
                           const void *buf,
                           size_t size) {
	if (zfp == NULL || zfp->filep == NULL)
		return -EBADF;

	if (buf == NULL && size != 0)
		return -EINVAL;

	struct devfs_fd *fd = zfp->filep;

	if (fd->entry->ops == NULL ||
	    fd->entry->ops->write == NULL)
		return -ENOTSUP;

	ssize_t ret =
	    fd->entry->ops->write(fd->entry, zfp, buf, size);

	if (ret > 0)
		fd->offset += ret;

	return ret;
}


static int devfs_seek(struct fs_file_t *zfp,
                      off_t offset,
                      int whence) {
	if (zfp == NULL || zfp->filep == NULL)
		return -EBADF;

	struct devfs_fd *fd = zfp->filep;

	if (fd->entry->ops == NULL ||
	    fd->entry->ops->seek == NULL)
		return -ESPIPE;

	int ret =
	    fd->entry->ops->seek(fd->entry, zfp, offset, whence);

	if (ret < 0)
		return ret;

	/*
	 * If a device implements custom seek semantics, tell() is the
	 * authoritative way to refresh our generic offset.
	 */
	if (fd->entry->ops->tell != NULL) {
		off_t pos =
		    fd->entry->ops->tell(fd->entry, zfp);

		if (pos >= 0)
			fd->offset = pos;
	}

	return 0;
}


static off_t devfs_tell(struct fs_file_t *zfp) {
	if (zfp == NULL || zfp->filep == NULL)
		return -EBADF;

	struct devfs_fd *fd = zfp->filep;

	if (fd->entry->ops != NULL &&
	    fd->entry->ops->tell != NULL)
		return fd->entry->ops->tell(fd->entry, zfp);

	return fd->offset;
}


static int devfs_truncate(struct fs_file_t *zfp, off_t length) {
	ARG_UNUSED(zfp);
	ARG_UNUSED(length);

	return 0;
}


static int devfs_sync(struct fs_file_t *zfp) {
	if (zfp == NULL || zfp->filep == NULL)
		return -EBADF;

	struct devfs_fd *fd = zfp->filep;

	if (fd->entry->ops != NULL &&
	    fd->entry->ops->sync != NULL)
		return fd->entry->ops->sync(fd->entry, zfp);

	return 0;
}


/***** DIRECTORIES *****/

static int devfs_opendir(struct fs_dir_t *zdp, const char *path) {
	if (zdp == NULL || zdp->mp == NULL || path == NULL)
		return -EINVAL;

	if (zdp->dirp != NULL)
		return -EBUSY;

	const struct devfs_entry *entries = zdp->mp->fs_data;
	if (entries == NULL)
		return -EINVAL;

	path = devfs_stripmnt(zdp->mp, path);

	/* The only directory in devfs is its root. */
	if (strcmp(path, "/") != 0)
		return -ENOTDIR;

	struct devfs_dir *dir = k_malloc(sizeof(*dir));
	if (dir == NULL)
		return -ENOMEM;

	dir->entries = entries;
	dir->index = 0;

	zdp->dirp = dir;
	return 0;
}


static int devfs_readdir(struct fs_dir_t *zdp,
                         struct fs_dirent *entry) {
	if (zdp == NULL || zdp->dirp == NULL || entry == NULL)
		return -EINVAL;

	struct devfs_dir *dir = zdp->dirp;

	if (!dir->entries[dir->index].name) {
		entry->name[0] = '\0';
		return 0;
	}

	const struct devfs_entry *dev = &dir->entries[dir->index++];

	memset(entry, 0, sizeof(*entry));

	entry->type = FS_DIR_ENTRY_FILE;
	entry->size = 0;

	strncpy(entry->name, dev->name,
	        sizeof(entry->name) - 1);

	return 0;
}


static int devfs_closedir(struct fs_dir_t *zdp) {
	if (zdp == NULL || zdp->dirp == NULL)
		return -EBADF;

	k_free(zdp->dirp);
	zdp->dirp = NULL;

	return 0;
}


/***** METADATA *****/

static int devfs_stat(struct fs_mount_t *mountp,
                      const char *path,
                      struct fs_dirent *entry) {
	if (mountp == NULL || path == NULL || entry == NULL)
		return -EINVAL;

	const struct devfs_entry *entries = mountp->fs_data;
	if (entries == NULL)
		return -EINVAL;

	path = devfs_stripmnt(mountp, path);

	memset(entry, 0, sizeof(*entry));

	if (strcmp(path, "/") == 0) {
		entry->type = FS_DIR_ENTRY_DIR;
		entry->size = 0;
		strncpy(entry->name, "/",
		        sizeof(entry->name) - 1);
		return 0;
	}

	const struct devfs_entry *dev = devfs_find(entries, path);
	if (dev == NULL)
		return -ENOENT;

	entry->type = FS_DIR_ENTRY_FILE;
	entry->size = 0;

	strncpy(entry->name, dev->name,
	        sizeof(entry->name) - 1);

	return 0;
}


static int devfs_unlink(struct fs_mount_t *mountp,
                        const char *path) {
	ARG_UNUSED(mountp);
	ARG_UNUSED(path);

	return -EPERM;
}


static int devfs_rename(struct fs_mount_t *mountp,
                        const char *from,
                        const char *to) {
	ARG_UNUSED(mountp);
	ARG_UNUSED(from);
	ARG_UNUSED(to);

	return -EPERM;
}


static int devfs_mkdir(struct fs_mount_t *mountp,
                       const char *path) {
	ARG_UNUSED(mountp);
	ARG_UNUSED(path);

	return -EPERM;
}


static int devfs_statvfs(struct fs_mount_t *mountp,
                         const char *path,
                         struct fs_statvfs *stat) {
	ARG_UNUSED(mountp);
	ARG_UNUSED(path);
	ARG_UNUSED(stat);

	return -ENOTSUP;
}


/***** MOUNT *****/

static int devfs_mount(struct fs_mount_t *mountp) {
	if (mountp == NULL || mountp->fs_data == NULL)
		return -EINVAL;

	const struct devfs_entry *entries = mountp->fs_data;

	const struct devfs_entry *cur = entries;
	while (cur->name) {
		if (!devfs_valid_name(cur->name))
			return -EINVAL;
		cur++;
	}

	return 0;
}


static int devfs_unmount(struct fs_mount_t *mountp) {
	ARG_UNUSED(mountp);
	return 0;
}


static struct fs_file_system_t devfs_api = {
	.open = devfs_open,
	.close = devfs_close,
	.read = devfs_read,
	.write = devfs_write,
	.lseek = devfs_seek,
	.tell = devfs_tell,
	.truncate = devfs_truncate,
	.sync = devfs_sync,

	.opendir = devfs_opendir,
	.readdir = devfs_readdir,
	.closedir = devfs_closedir,

	.mount = devfs_mount,
	.unmount = devfs_unmount,

	.unlink = devfs_unlink,
	.rename = devfs_rename,
	.mkdir = devfs_mkdir,

	.stat = devfs_stat,
	.statvfs = devfs_statvfs,
};


int devfs_register(void) {
	return fs_register(FS_DEVFS, &devfs_api);
}

SYS_INIT(devfs_register, POST_KERNEL, CONFIG_DEVFS_INIT_PRIORITY);
