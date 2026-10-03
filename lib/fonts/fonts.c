#include <fonts.h>

size_t font_glyph_bitmap_size(const struct font *font) {
	return font->height * font->stride;
}

bool font_has_glyph(const struct font *font, uint32_t codepoint) {
	if (codepoint < font->glyph_min || codepoint > font->glyph_max)
		return false;

	for (size_t i = 0; i < font->range_count; i++) {
		uint32_t begin = font->ranges[i * 2];
		uint32_t end = font->ranges[i * 2 + 1];

		if (codepoint >= begin && codepoint < end)
			return true;
	}
	return false;
}

const uint8_t *font_get_glyph(const struct font *font, uint32_t codepoint) {
	/* offset 0 would be the .nodef-character */
	size_t offset = 1;


	if (codepoint < font->glyph_min || codepoint > font->glyph_max)
		return font->data; /* glyph not found, return .nodef */

	for (size_t i = 0; i < font->range_count; i++) {
		uint32_t begin = font->ranges[i * 2];
		uint32_t end = font->ranges[i * 2 + 1];

		if (codepoint >= begin && codepoint < end) {
			size_t index = offset + (codepoint - begin);

			return font->data + index * font_glyph_bitmap_size(font);
		}

		offset += end - begin;
	}

	return font->data; /* glyph not found, return .nodef */
}

uint8_t font_glyph_coverage(const struct font *font, const uint8_t *glyph, uint16_t x, uint16_t y) {
	if (x >= font->width || y >= font->height)
		return 0;

	uint8_t byte = glyph[y * font->stride + x / 2];

	return (x & 1)
	         ? byte & 0x0f
	         : byte >> 4;
}
