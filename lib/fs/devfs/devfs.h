#pragma once

#include <stddef.h>
#include <sys/types.h>
#include <zephyr/fs/fs.h>

struct devfs_entry;

struct devfs_ops {
	int (*open)(const struct devfs_entry *entry,
	            struct fs_file_t *file,
	            fs_mode_t flags);

	int (*close)(const struct devfs_entry *entry,
	             struct fs_file_t *file);

	ssize_t (*read)(const struct devfs_entry *entry,
	                struct fs_file_t *file,
	                void *buf,
	                size_t size);

	ssize_t (*write)(const struct devfs_entry *entry,
	                 struct fs_file_t *file,
	                 const void *buf,
	                 size_t size);

	int (*seek)(const struct devfs_entry *entry,
	            struct fs_file_t *file,
	            off_t offset,
	            int whence);

	off_t (*tell)(const struct devfs_entry *entry,
	              struct fs_file_t *file);

	int (*sync)(const struct devfs_entry *entry,
	            struct fs_file_t *file);
};

struct devfs_entry {
	const char *name;
	const struct devfs_ops *ops;
	void *userdata;
};
