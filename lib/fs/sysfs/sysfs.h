#pragma once


#include <stdbool.h>
#include <stddef.h>
#include <zephyr/fs/fs.h>

enum {
	FS_SYSFS = FS_TYPE_EXTERNAL_BASE + 1
};

struct sysfs_fd {
	bool used;
	const struct sysfs_entry *entry;
	off_t offset;
};

struct sysfs_entry {
	enum fs_dir_entry_type type;
	const char *name;
	const struct sysfs_entry *parent;
	off_t size; /* entry-count of file-size */

	union {
		const uint8_t *content;                   /* when file */
		const struct sysfs_entry *const *entries; /* when dir */
	};
};

struct sysfs_fs {
	const struct sysfs_entry *root;

	struct sysfs_fd fds[CONFIG_SYSFS_MAX_FDS];
};
