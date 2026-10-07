#pragma once

#include <zephyr/fs/fs.h>


enum {
	FS_SYSFS = FS_TYPE_EXTERNAL_BASE,
};


int sysfs_register();
