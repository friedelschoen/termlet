#pragma once

#include <fonts.h>
#include <window/geom.h>
#include <zephyr/fs/fs.h>

#define DISPLAY_WIDTH  DT_PROP(DT_CHOSEN(zephyr_display), width)
#define DISPLAY_HEIGHT DT_PROP(DT_CHOSEN(zephyr_display), height)

struct renderer {
	const struct device *display;
	struct win_rect dirty;

	unsigned int columns;
	unsigned int rows;
};

void draw_glyph(struct renderer *rend, uint32_t codepoint, uint16_t x, uint16_t y, const struct font *font, uint16_t fg, uint16_t bg);
void draw_commit(struct renderer *rend);

int sysfs_register();
