#pragma once

#include "geom.h"

#include <stdint.h>

struct win_char {
	uint32_t code;
	uint16_t fg; /* RGB565 */
	uint16_t bg; /* RGB565 */
};

static inline struct win_char *win_chars_get(struct win_char *chars, struct win_rect bounds, int x, int y) {
	int stride = win_rect_width(bounds);
	return &chars[y * stride + x];
}
