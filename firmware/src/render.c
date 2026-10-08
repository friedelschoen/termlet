#include "internal.h"
#include "zephyr/sys/printk.h"

#include <stdint.h>
#include <window/geom.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>


static uint16_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];

static inline uint16_t blend_rgb565(uint16_t bg, uint16_t fg, uint8_t coverage) {
	uint32_t br = (bg >> 11) & 0x1f;
	uint32_t bgc = (bg >> 5) & 0x3f;
	uint32_t bb = bg & 0x1f;

	uint32_t fr = (fg >> 11) & 0x1f;
	uint32_t fgc = (fg >> 5) & 0x3f;
	uint32_t fb = fg & 0x1f;

	uint32_t r = (br * (15 - coverage) + fr * coverage) / 15;
	uint32_t g = (bgc * (15 - coverage) + fgc * coverage) / 15;
	uint32_t b = (bb * (15 - coverage) + fb * coverage) / 15;

	return (uint16_t) ((r << 11) | (g << 5) | b);
}

void draw_glyph(struct renderer *rend, uint32_t codepoint, uint16_t x, uint16_t y, const struct font *font, uint16_t fg, uint16_t bg) {

	const uint8_t *glyph = font_get_glyph(font, codepoint);
	uint16_t colors[16];

	for (unsigned int i = 0; i < ARRAY_SIZE(colors); i++)
		colors[i] = blend_rgb565(bg, fg, i);

	for (unsigned int gy = 0; gy < font->height; gy++) {
		unsigned int py = y * font->height + gy;

		for (unsigned int gx = 0; gx < font->width; gx++) {
			unsigned int px = x * font->width + gx;

			uint8_t coverage = font_glyph_coverage(font, glyph, gx, gy);
			framebuffer[py * DISPLAY_WIDTH + px] = colors[coverage];
		}
	}

	struct win_rect drawn = {
		.x0 = x,
		.x1 = x + font->width,
		.y0 = y,
		.y1 = y + font->height,
	};

	win_rect_include_rect(&rend->dirty, drawn);
}

void draw_commit(struct renderer *rend) {
	struct display_buffer_descriptor desc = {
		.buf_size = DISPLAY_WIDTH * DISPLAY_HEIGHT,
		.width = DISPLAY_WIDTH,
		.height = DISPLAY_HEIGHT,
		.pitch = DISPLAY_WIDTH,
		.frame_incomplete = false,
	};

	display_write(rend->display, 0, 0, &desc, framebuffer);
}
