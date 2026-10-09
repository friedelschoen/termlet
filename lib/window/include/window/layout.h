#pragma once

#include "geom.h"

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/input/input.h>


/** Character attributes. */
enum {
	WIN_CHAR_BOLD = (1 << 0),
	WIN_CHAR_ITALIC = (1 << 1),
};

/** Character settings */
struct win_char {
	uint32_t code; /**< UTF32 codepoint. */
	uint16_t fg;   /**< Text color denoted as RGB565. */
	uint16_t bg;   /**< Background color denoted as RGB565. */
	uint8_t attr;  /**< Additional attributes. */
};

/* (in drawing order) */
enum win_mode {
	/**
	 * Window takes up remaining space.
	 *
	 * @c width , @c height and @c edge are ignored.
	 *
	 */
	WIN_MODE_MAIN,
	/**
	 * Window is clipped to an edge.
	 *
	 * @c edge specifies the edge it clips to.
	 * @c width is ignored if this window is clipped to the bottom or top edge.
	 * @c height is ignored if this window is clipped to the left or right edge.
	 */
	WIN_MODE_CLIP,
	/**
	 * Window is clipped to an edge but overlaying main and/or other clip-windows.
	 *
	 * @c edge specifies the edge it clips to.
	 * @c width is ignored if this window is clipped to the bottom or top edge.
	 * @c height is ignored if this window is clipped to the left or right edge.
	 */
	WIN_MODE_OVERLAY,
	/**
	 * Window floats centered over whole screen.
	 *
	 * @c width and @c height are respected, @c edge is ignored.
	 */
	WIN_MODE_DIALOG,
};

/** Edges a window can clip to. */
enum win_edge {
	WIN_EDGE_TOP,
	WIN_EDGE_BOTTOM,
	WIN_EDGE_LEFT,
	WIN_EDGE_RIGHT,
};

struct win_layout;
struct win_state;

/** This handler is called right before the window is drawn to screen */
typedef void (*win_render_handler_t)(struct win_state *w, struct win_rect redraw, void *userdata);


/** This handler is called whenever the window is resized or its visibility changed */
typedef void (*win_layout_handler_t)(struct win_state *w,
                                     uint16_t cols, uint16_t rows,
                                     bool visible, void *userdata);

/** This handler is called on input, depending on current focus. */
typedef void (*win_input_handler_t)(struct win_state *w,
                                    struct input_event *evt, void *userdata);

struct win_state {
	struct win_layout *l; /**< layout this window belongs to */

	bool enabled;   /**< user requests this window to be visible */
	bool focusable; /**< can grab keyboard-focus */

	enum win_mode mode; /**< window layout mode */
	enum win_edge edge; /**< if clip/overlay: this is the edge it is attached to */
	uint8_t z_index;    /**< window layer, lower value -> higher up (more visible) */

	/* requested geometry */
	uint16_t width;  /**< requested width, might be ignored depending on window-mode */
	uint16_t height; /**< requested height, might be ignored depending on window-height */

	/* callbacks */
	win_render_handler_t render;
	void *render_userdata;
	win_layout_handler_t layout;
	void *layout_userdata;
	win_input_handler_t input;
	void *input_userdata;

	/* set by layout_win_apply(). NO TOUCHY TOUCHY! */
	bool used;                    /**< READONLY. if this slot is being used */
	bool visible;                 /**< READONLY. whether window is visible and not shadowed */
	struct win_rect win_bounds;   /**< READONLY. window bounds, incl. border */
	struct win_rect chars_bounds; /**< READONLY. content bounds, excl. border */
};

struct win_layout {
	struct win_state windows[CONFIG_WINDOW_LAYOUT_MAX_WINDOWS];

	struct win_state *draw_order[CONFIG_WINDOW_LAYOUT_MAX_WINDOWS];
	int draw_count;

	struct win_rect bounds;
	struct win_rect dirty;
	struct win_state *focus;

	void (*draw_char)(uint16_t x, uint16_t y, struct win_char ch, void *userdata);
	void (*commit)(void *userdata);
	void *userdata;
};

struct win_state *win_layout_new(struct win_layout *l);
void win_layout_remove(struct win_state *w);

/**
 * Updates the current layout. Must be called whenever window attributes change.
 */
void win_layout_update(struct win_layout *l);

/**
 * Draws characters using draw_char callback.
 * It is meant to be called while in render_handler and
 * might ignore calls outside handler.
 */
void win_layout_draw_char(struct win_state *w, int x, int y, struct win_char ch);
void win_layout_input(struct input_event *ev);
void win_layout_render(struct win_layout *l);
