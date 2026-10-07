#pragma once

#include <zephyr/fs/fs.h>

enum {
	FS_SYSFS = FS_TYPE_EXTERNAL_BASE + 1,
	FS_DEVFS,
	FS_TMPFS,
};
