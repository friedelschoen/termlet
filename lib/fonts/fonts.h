#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct font {
	const char *name;
	const char *style;
	uint16_t size;

	uint16_t width;
	uint16_t height;
	uint16_t stride;

	const uint8_t *data;
	const uint32_t *ranges;
	size_t range_count;

	uint32_t glyph_min;
	uint32_t glyph_max;
};

extern const struct font *const fonts[];
extern const size_t font_count;

/**
 * Check if a font has a specific glyph.
 */
bool font_has_glyph(const struct font *font, uint32_t codepoint);

/**
 * Get glyph bitmap of a glyph. If glyph is not defined, a replacement character is returned.
 */
const uint8_t *font_get_glyph(const struct font *fnt, uint32_t glyph);

/**
 * Get size of a bitmap of a glyph
 */
size_t font_glyph_bitmap_size(const struct font *font);

/**
 * Get coverage of a pixel in a glyph bitmap
 */
uint8_t font_glyph_coverage(const struct font *font, const uint8_t *glyph, uint16_t x, uint16_t y);
