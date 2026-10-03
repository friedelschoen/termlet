#include "libutf8.h"

int utf8_decode(const char *text, int text_len, uint32_t *out) {
	const uint8_t *s = (const uint8_t *) text;
	uint32_t cp;

	if (text_len == 0)
		return 0;

	if (text_len >= 1 && s[0] < 0x80) {
		*out = s[0];
		return 1;
	}

	if (text_len >= 2 && (s[0] & 0xe0) == 0xc0) {
		if ((s[1] & 0xc0) != 0x80)
			return -1;

		cp = ((uint32_t) (s[0] & 0x1f) << 6) |
		     (uint32_t) (s[1] & 0x3f);

		if (cp < 0x80)
			return -1; /* overlong */

		*out = cp;
		return 2;
	}

	if (text_len >= 3 && (s[0] & 0xf0) == 0xe0) {
		if ((s[1] & 0xc0) != 0x80 ||
		    (s[2] & 0xc0) != 0x80)
			return -1;

		cp = ((uint32_t) (s[0] & 0x0f) << 12) |
		     ((uint32_t) (s[1] & 0x3f) << 6) |
		     (uint32_t) (s[2] & 0x3f);

		if (cp < 0x800)
			return -1; /* overlong */

		if (cp >= 0xd800 && cp <= 0xdfff)
			return -1; /* UTF-16 surrogate */

		*out = cp;
		return 3;
	}

	if (text_len >= 4 && (s[0] & 0xf8) == 0xf0) {
		if ((s[1] & 0xc0) != 0x80 ||
		    (s[2] & 0xc0) != 0x80 ||
		    (s[3] & 0xc0) != 0x80)
			return -1;

		cp = ((uint32_t) (s[0] & 0x07) << 18) |
		     ((uint32_t) (s[1] & 0x3f) << 12) |
		     ((uint32_t) (s[2] & 0x3f) << 6) |
		     (uint32_t) (s[3] & 0x3f);

		if (cp < 0x10000)
			return -1; /* overlong */

		if (cp > 0x10ffff)
			return -1;

		*out = cp;
		return 4;
	}

	*out = UINT32_MAX;
	return 1;
}

int utf8_encode(char *text, int text_len, uint32_t cp) {
	if (cp <= 0x7f) {
		if (text_len < 1)
			return 0;

		text[0] = (char) cp;
		return 1;
	}

	if (cp <= 0x7ff) {
		if (text_len < 2)
			return 0;

		text[0] = (char) (0xc0 | (cp >> 6));
		text[1] = (char) (0x80 | (cp & 0x3f));
		return 2;
	}

	if (cp >= 0xd800 && cp <= 0xdfff)
		return 0;

	if (cp <= 0xffff) {
		if (text_len < 3)
			return -1;

		text[0] = (char) (0xe0 | (cp >> 12));
		text[1] = (char) (0x80 | ((cp >> 6) & 0x3f));
		text[2] = (char) (0x80 | (cp & 0x3f));
		return 3;
	}

	if (cp <= 0x10ffff) {
		if (text_len < 4)
			return -1;

		text[0] = (char) (0xf0 | (cp >> 18));
		text[1] = (char) (0x80 | ((cp >> 12) & 0x3f));
		text[2] = (char) (0x80 | ((cp >> 6) & 0x3f));
		text[3] = (char) (0x80 | (cp & 0x3f));
		return 4;
	}

	return 0;
}
