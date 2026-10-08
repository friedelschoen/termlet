#pragma once

#include "geom.h"

#include <stdbool.h>
#include <stdint.h>


enum {
	WIN_CHAR_BOLD = (1 << 0),
	WIN_CHAR_ITALIC = (1 << 1),
};

struct win_char {
	uint32_t code;
	uint16_t fg; /* RGB565 */
	uint16_t bg; /* RGB565 */
	uint8_t attr;
};

/** (in drawing order) */
enum win_mode {
	WIN_MODE_MAIN,    /**< window takes up remaining space */
	WIN_MODE_CLIP,    /**< window is clipped to an edge */
	WIN_MODE_OVERLAY, /**< window is clipped to an edge but overlaying main and/or other clip-windows */
	WIN_MODE_DIALOG,  /**< window floats centered */
};

enum win_edge {
	WIN_EDGE_TOP,
	WIN_EDGE_BOTTOM,
	WIN_EDGE_LEFT,
	WIN_EDGE_RIGHT,
};

struct win_layout;

/** This handler is called right before the window is drawn to screen */
typedef void (*win_render_handler_t)(struct win_layout *layout, int win, struct win_rect redraw, void *userdata);


/** This handler is called whenever the window is resized or its visibility changed */
typedef void (*win_layout_handler_t)(struct win_layout *layout, int win,
                                     uint16_t cols, uint16_t rows,
                                     bool visible, void *userdata);

struct win_state {
	bool used;    /**< if this slot is being used */
	bool enabled; /**< user requests this window to be visible */
	bool visible; /**< whether window is visible and not shadowed */

	enum win_mode mode;
	enum win_edge edge;
	uint8_t z_index;

	/* requested geometry */
	uint16_t width;
	uint16_t height;

	win_render_handler_t render;
	void *render_userdata;
	win_layout_handler_t layout;
	void *layout_userdata;

	struct win_rect win_bounds;   /* window bounds, incl. border */
	struct win_rect chars_bounds; /* content bounds, excl. border */
};

struct win_layout {
	struct win_state windows[CONFIG_WINDOW_LAYOUT_MAX_WINDOWS];

	int draw_order[CONFIG_WINDOW_LAYOUT_MAX_WINDOWS];
	int draw_count;

	struct win_rect bounds;
	struct win_rect dirty;

	void (*draw_char)(uint16_t x, uint16_t y, struct win_char ch, void *userdata);
	void (*commit)(void *userdata);
	void *userdata;
};

int win_layout_new_main(struct win_layout *l, uint8_t z_index);
int win_layout_new_dialog(struct win_layout *l, uint8_t z_index, int width, int height);
int win_layout_new_clip(struct win_layout *l, uint8_t z_index, enum win_edge edge, int size);
int win_layout_new_overlay(struct win_layout *l, uint8_t z_index, enum win_edge edge, int size);
void win_layout_remove(struct win_layout *l, int win);

void win_layout_set_render(struct win_layout *l, int win, win_render_handler_t render, void *userdata);
void win_layout_set_layout(struct win_layout *l, int win, win_layout_handler_t layout, void *userdata);
void win_layout_enable(struct win_layout *l, int win, bool enabled);
void win_layout_resize(struct win_layout *l, int win, int w, int h);

/** draws characters using draw_char callback. It is meant to be called while in
 render_handler and might ignore calls outside handler. */
void win_layout_draw_char(struct win_layout *l, int win, int x, int y, struct win_char ch);
void win_layout_render(struct win_layout *l);
