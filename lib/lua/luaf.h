#pragma once

#include <stddef.h>
#include <zephyr/fs/fs.h>

struct luaf {
	struct fs_file_t fd;
	int open;
    int error;
	int eof;
	int ungot; /* EOF means empty */
};

void luaf_init(struct luaf *f);

int luaf_open(struct luaf *f, const char *path, const char *mode);
int luaf_close(struct luaf *f);

size_t luaf_read(void *ptr, size_t size, size_t nmemb, struct luaf *f);
size_t luaf_write(const void *ptr, size_t size, size_t nmemb, struct luaf *f);

int luaf_getc(struct luaf *f);
int luaf_putc(int c, struct luaf *f);
int luaf_ungetc(int c, struct luaf *f);

int luaf_seek(struct luaf *f, off_t offset, int whence);
off_t luaf_tell(struct luaf *f);
void luaf_rewind(struct luaf *f);

int luaf_flush(struct luaf *f);

int luaf_isopen(const struct luaf *f);
int luaf_error(const struct luaf *f);
int luaf_eof(const struct luaf *f);
void luaf_clearerr(struct luaf *f);
