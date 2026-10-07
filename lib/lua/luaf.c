#include "luaf.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int mode_to_flags(const char *mode, fs_mode_t *flags) {
	if (!mode || !mode[0])
		return -EINVAL;

	if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
		*flags = FS_O_READ;
		return 0;
	}

	if (strcmp(mode, "r+") == 0 || strcmp(mode, "rb+") == 0 ||
	    strcmp(mode, "r+b") == 0) {
		*flags = FS_O_RDWR;
		return 0;
	}

	if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
		*flags = FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC;
		return 0;
	}

	if (strcmp(mode, "w+") == 0 || strcmp(mode, "wb+") == 0 ||
	    strcmp(mode, "w+b") == 0) {
		*flags = FS_O_RDWR | FS_O_CREATE | FS_O_TRUNC;
		return 0;
	}

	if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
		*flags = FS_O_WRITE | FS_O_CREATE | FS_O_APPEND;
		return 0;
	}

	if (strcmp(mode, "a+") == 0 || strcmp(mode, "ab+") == 0 ||
	    strcmp(mode, "a+b") == 0) {
		*flags = FS_O_RDWR | FS_O_CREATE | FS_O_APPEND;
		return 0;
	}

	return -EINVAL;
}

void luaf_init(struct luaf *f) {
	fs_file_t_init(&f->fd);
	f->error = 0;
	f->eof = 0;
    f->open = 0;
    f->ungot = EOF;
}

int luaf_open(struct luaf *f, const char *path, const char *mode) {
	fs_mode_t flags;
	int ret;

	luaf_init(f);

	ret = mode_to_flags(mode, &flags);
	if (ret < 0) {
		f->error = ret;
		return ret;
	}

	ret = fs_open(&f->fd, path, flags);
	if (ret < 0) {	
      f->error = ret;
	  return ret;
    }
    f->open = true;
    return 0;
}

int luaf_close(struct luaf *f) {
	int ret = fs_close(&f->fd);

	if (ret < 0) {
      f->error = ret;
      return ret;
    }

    f->open = false;
	return 0;
}

size_t luaf_read(void *ptr, size_t size, size_t nmemb, struct luaf *f) {
    char* b = ptr;
	size_t bytes;
	ssize_t ret;

	if (size == 0 || nmemb == 0)
		return 0;

	if (nmemb > SIZE_MAX / size) {
		f->error = -EOVERFLOW;
		return 0;
	}

    bytes = size * nmemb;
    if (f->ungot != EOF) {
        *b = f->ungot;
        b++;
        bytes--;
    }

	ret = fs_read(&f->fd, b, bytes);
	if (ret < 0) {
		f->error = (int) ret;
		return 0;
	}

	if (ret == 0) {
		f->eof = 1;
		return 0;
	}

	/*
	 * Match fread(): return the number of complete elements read.
	 *
	 * Note that if fs_read() returns a partial element, those bytes are
	 * already present in ptr even though they do not contribute to the
	 * return value.
	 */
	return (size_t) ret / size;
}

size_t luaf_write(const void *ptr, size_t size, size_t nmemb, struct luaf *f) {
	size_t bytes;
	ssize_t ret;

	if (size == 0 || nmemb == 0)
		return 0;

	if (nmemb > SIZE_MAX / size) {
		f->error = -EOVERFLOW;
		return 0;
	}

	bytes = size * nmemb;

	ret = fs_write(&f->fd, ptr, bytes);
	if (ret < 0) {
		f->error = (int) ret;
		return 0;
	}

	if ((size_t) ret < bytes) {
		/*
		 * Zephyr permits a short write. Unfortunately the filesystem
		 * API does not provide a portable error value here unless the
		 * backend also sets errno.
		 *
		 * Record a generic I/O error for stdio-like semantics.
		 */
		f->error = -EIO;
	}

	return (size_t) ret / size;
}

int luaf_getc(struct luaf *f) {
	unsigned char c;
	ssize_t ret;

	if (f->ungot != EOF) {
		int ch = f->ungot;
		f->ungot = EOF;
		return ch;
	}

	ret = fs_read(&f->fd, &c, 1);

	if (ret == 1)
		return c;

	if (ret < 0)
		f->error = (int)ret;

    f->eof = 1;
	return EOF;
}

int luaf_ungetc(int c, struct luaf *f) {
	if (c == EOF)
		return EOF;

	if (f->ungot != EOF)
		return EOF;

	f->ungot = (unsigned char)c;
	return (unsigned char)c;
}

int luaf_putc(int c, struct luaf *f) {
	unsigned char ch = (unsigned char) c;
	ssize_t ret;

	ret = fs_write(&f->fd, &ch, 1);

	if (ret == 1)
		return ch;

	if (ret < 0)
		f->error = (int) ret;
	else
		f->error = -EIO;

	return EOF;
}

int luaf_seek(struct luaf *f, off_t offset, int whence) {
	int zwhence;
	int ret;

	switch (whence) {
		case SEEK_SET:
			zwhence = FS_SEEK_SET;
			break;

		case SEEK_CUR:
			zwhence = FS_SEEK_CUR;
			break;

		case SEEK_END:
			zwhence = FS_SEEK_END;
			break;

		default:
			f->error = -EINVAL;
			return -1;
	}

	ret = fs_seek(&f->fd, offset, zwhence);
	if (ret < 0) {
		f->error = ret;
		return -1;
	}

	f->eof = 0;
    f->ungot = EOF;
	return 0;
}

off_t luaf_tell(struct luaf *f) {
	off_t ret = fs_tell(&f->fd);

	if (ret < 0)
		f->error = (int) ret;

	return ret;
}

void luaf_rewind(struct luaf *f) {
	if (fs_seek(&f->fd, 0, FS_SEEK_SET) < 0) {
		/*
		 * rewind() cannot report failure, so preserve it through
		 * luaf_error().
		 */
		f->error = -EIO;
		return;
	}

    f->ungot = EOF;
	f->error = 0;
	f->eof = 0;
}

int luaf_flush(struct luaf *f) {
	int ret = fs_sync(&f->fd);

	if (ret < 0) {
		f->error = ret;
		return EOF;
	}

	return 0;
}

int luaf_isopen(const struct luaf *f) {
	return f->open;
}

int luaf_error(const struct luaf *f) {
	return f->error;
}

int luaf_eof(const struct luaf *f) {
	return f->eof;
}

void luaf_clearerr(struct luaf *f) {
	f->error = 0;
	f->eof = 0;
}
