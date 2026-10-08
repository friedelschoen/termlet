#include <assert.h>
#include <window/geom.h>
#include <window/layout.h>
#include <zephyr/sys/util.h>

static int win_alloc(struct win_layout *l) {
	for (int i = 0; i < (int) ARRAY_SIZE(l->windows); i++) {
		if (!l->windows[i].used) {
			memset(&l->windows[i], 0, sizeof(*l->windows));
			l->windows[i].used = true;
			return i;
		}
	}
	return -1;
}

static int win_layout_layer(const struct win_state *w) {
	switch (w->mode) {
		case WIN_MODE_CLIP:
		case WIN_MODE_OVERLAY:
			return 0;

		case WIN_MODE_MAIN:
			return 1;

		case WIN_MODE_DIALOG:
			return 2;
	}

	return 0;
}

static bool win_layout_ordered(const struct win_state *prev, const struct win_state *cur) {
	int prev_layer = win_layout_layer(prev);
	int cur_layer = win_layout_layer(cur);

	if (prev_layer != cur_layer)
		return prev_layer < cur_layer;

	/*
	 * Lower z-index has higher layout priority and therefore
	 * comes first.
	 */
	if (prev->z_index != cur->z_index)
		return prev->z_index < cur->z_index;

	if ((prev->mode == WIN_MODE_CLIP ||
	     prev->mode == WIN_MODE_OVERLAY) &&
	    prev->edge != cur->edge)
		return prev->edge < cur->edge;

	return true; /* stable */
}

static bool win_draw_ordered(const struct win_state *prev, const struct win_state *cur) {
	if (prev->mode != cur->mode)
		return prev->mode < cur->mode;

	if (prev->z_index != cur->z_index)
		return prev->z_index > cur->z_index;

	if ((prev->mode == WIN_MODE_CLIP ||
	     prev->mode == WIN_MODE_OVERLAY) &&
	    prev->edge != cur->edge)
		return prev->edge < cur->edge;

	return true;
}

static void win_layout_apply_bounds_edge(struct win_state *w, struct win_rect *a, uint16_t *edge_shadow) {
	if (win_rect_empty(*a))
		return;

	uint16_t size = 0;

	switch (w->edge) {
		case WIN_EDGE_TOP:
		case WIN_EDGE_BOTTOM:
			size = w->height;

			if (win_rect_height(*a) < size + 1)
				return;

			w->win_bounds.x0 = a->x0;
			w->chars_bounds.x0 = a->x0;
			w->win_bounds.x1 = a->x1;
			w->chars_bounds.x1 = a->x1;

			if (w->edge == WIN_EDGE_TOP) {
				w->win_bounds.y0 = a->y0;
				w->chars_bounds.y0 = a->y0;
				w->win_bounds.y1 = a->y0 + size + 1;
				w->chars_bounds.y1 = a->y0 + size;
			} else {
				w->win_bounds.y0 = a->y1 - size - 1;
				w->chars_bounds.y0 = a->y1 - size;
				w->win_bounds.y1 = a->y1;
				w->chars_bounds.y1 = a->y1;
			}
			break;

		case WIN_EDGE_LEFT:
		case WIN_EDGE_RIGHT:
			size = w->width;

			if (win_rect_width(*a) < size)
				return;

			w->win_bounds.y0 = a->y0;
			w->chars_bounds.y0 = a->y0;
			w->win_bounds.y1 = a->y1;
			w->chars_bounds.y1 = a->y1;

			if (w->edge == WIN_EDGE_LEFT) {
				w->win_bounds.x0 = a->x0;
				w->chars_bounds.x0 = a->x0;
				w->win_bounds.x1 = a->x0 + size + 1;
				w->chars_bounds.x1 = a->x0 + size;
			} else {
				w->win_bounds.x0 = a->x1 - size - 1;
				w->chars_bounds.x0 = a->x1 - size;
				w->win_bounds.x1 = a->x1;
				w->chars_bounds.x1 = a->x1;
			}
			break;
	}

	if (w->mode == WIN_MODE_OVERLAY) {
		/*
		 * Overlays don't consume available space. Multiple
		 * overlays on the same edge overlap from the same
		 * origin, so their shadow is the maximum size rather
		 * than the sum.
		 */
		edge_shadow[w->edge] =
		    MAX(edge_shadow[w->edge], size + 1);

		w->visible = true;
		return;
	}

	/*
	 * A clip still consumes layout space even when it is
	 * completely hidden behind an overlay.
	 */
	w->visible = edge_shadow[w->edge] < size + 1;

	switch (w->edge) {
		case WIN_EDGE_TOP:
			a->y0 += size + 1;
			break;

		case WIN_EDGE_BOTTOM:
			a->y1 -= size + 1;
			break;

		case WIN_EDGE_LEFT:
			a->x0 += size + 1;
			break;

		case WIN_EDGE_RIGHT:
			a->x1 -= size + 1;
			break;
	}

	/*
	 * Moving the available edge through this clip also moves
	 * past the corresponding part of the overlay shadow.
	 */
	if (edge_shadow[w->edge] > size + 1)
		edge_shadow[w->edge] -= size + 1;
	else
		edge_shadow[w->edge] = 0;
}

static void win_layout_apply_bounds(struct win_layout *l, int *order, int win, struct win_rect *a, bool *main_done, uint16_t *edge_shadow) {
	struct win_state *w = &l->windows[order[win]];

	/* reset state */
	w->visible = false;

	switch (w->mode) {
		case WIN_MODE_MAIN:
			if (*main_done || win_rect_empty(*a))
				break;

			w->win_bounds = *a;
			w->chars_bounds = *a;
			w->visible = true;
			*main_done = true;
			break;

		case WIN_MODE_DIALOG: {
			if (w->width + 2 > win_rect_width(l->bounds) ||
			    w->height + 2 > win_rect_height(l->bounds))
				break;

			uint16_t x = l->bounds.x0 +
			             (win_rect_width(l->bounds) - w->width) / 2;
			uint16_t y = l->bounds.y0 +
			             (win_rect_height(l->bounds) - w->height) / 2;

			w->chars_bounds = (struct win_rect){
				.x0 = x,
				.y0 = y,
				.x1 = x + w->width,
				.y1 = y + w->height,
			};

			w->win_bounds = (struct win_rect){
				.x0 = x - 1,
				.y0 = y - 1,
				.x1 = x + w->width + 1,
				.y1 = y + w->height + 1,
			};

			bool covered = false;

			for (int j = 0; j < win; j++) {
				struct win_state *other = &l->windows[order[j]];

				if (other->mode != WIN_MODE_DIALOG ||
				    !other->visible)
					continue;

				if (win_rect_contains_rect(other->win_bounds,
				                           w->win_bounds)) {
					covered = true;
					break;
				}
			}

			if (!covered)
				w->visible = true;

			break;
		}

		case WIN_MODE_CLIP:
		case WIN_MODE_OVERLAY:
			win_layout_apply_bounds_edge(w, a, edge_shadow);
			break;
	}
}

static void win_layout_apply(struct win_layout *l, int *order, int order_count) {
	struct win_rect a = l->bounds;
	uint16_t edge_shadow[4] = { 0 };
	bool main_done = false;

	for (int win = 0; win < order_count; win++) {
		struct win_state *w = &l->windows[order[win]];

		struct win_rect old_chars_bounds = w->chars_bounds;
		struct win_rect old_win_bounds = w->win_bounds;
		bool old_visible = w->visible;

		win_layout_apply_bounds(l, order, win, &a, &main_done, edge_shadow);

		if (!win_rect_equals(w->chars_bounds, old_chars_bounds) || w->visible != old_visible) {
			win_rect_include_rect(&l->dirty, old_win_bounds);
			if (w->visible)
				win_rect_include_rect(&l->dirty, w->win_bounds);

			if (w->layout)
				w->layout(
				    l,
				    order[win],
				    win_rect_width(w->chars_bounds),
				    win_rect_height(w->chars_bounds),
				    w->visible,
				    w->layout_userdata);
		}
	}
}

/** Compares cur with previous. @returns whether @p cur should be placed after @p prev. */
typedef bool (*win_comparator_t)(const struct win_state *prev, const struct win_state *cur);

static int win_sort(struct win_layout *l, int *order, int check_visible, win_comparator_t compare) {
	int order_count = 0;

	for (int i = 0; i < (int) ARRAY_SIZE(l->windows); i++) {
		struct win_state *w = &l->windows[i];

		if ((check_visible && !w->visible) || !w->used || !w->enabled)
			continue;

		int j = order_count;

		while (j > 0) {
			int prev = order[j - 1];

			if (compare(&l->windows[prev], w))
				break;

			order[j] = prev;
			j--;
		}

		order[j] = i;
		order_count++;
	}
	return order_count;
}

void win_layout_update(struct win_layout *l) {
	int order[CONFIG_WINDOW_LAYOUT_MAX_WINDOWS];
	int order_count;

	order_count = win_sort(l, order, false, win_layout_ordered);

	win_layout_apply(l, order, order_count);

	l->draw_count = win_sort(l, l->draw_order, true, win_draw_ordered);
}

int win_layout_new_main(struct win_layout *l, uint8_t z_index) {
	int handle = win_alloc(l);
	if (handle < 0)
		return handle;

	struct win_state *w = &l->windows[handle];
	w->mode = WIN_MODE_MAIN;
	w->z_index = z_index;

	win_layout_update(l);

	return handle;
}

int win_layout_new_dialog(struct win_layout *l, uint8_t z_index, int width, int height) {
	int handle = win_alloc(l);
	if (handle < 0)
		return handle;

	struct win_state *w = &l->windows[handle];
	w->mode = WIN_MODE_DIALOG;
	w->z_index = z_index;
	w->width = width;
	w->height = height;

	win_layout_update(l);

	return handle;
}

static int win_layout_new_edge(struct win_layout *l, enum win_mode mode, uint8_t z_index, enum win_edge edge, int size) {
	int handle = win_alloc(l);
	if (handle < 0)
		return handle;

	struct win_state *w = &l->windows[handle];
	w->mode = mode;
	w->z_index = z_index;
	w->edge = edge;

	/* this does not mean we square, but width is ignored if edge=TOP/BOTTOM and same for height */
	w->width = size;
	w->height = size;

	win_layout_update(l);

	return handle;
}

int win_layout_new_clip(struct win_layout *l, uint8_t z_index, enum win_edge edge, int size) {
	return win_layout_new_edge(l, WIN_MODE_CLIP, z_index, edge, size);
}

int win_layout_new_overlay(struct win_layout *l, uint8_t z_index, enum win_edge edge, int size) {
	return win_layout_new_edge(l, WIN_MODE_OVERLAY, z_index, edge, size);
}

void win_layout_remove(struct win_layout *l, int win) {
	struct win_state *w = &l->windows[win];

	if (w->used && w->visible)
		win_rect_include_rect(&l->dirty, w->win_bounds);

	w->used = false;
	win_layout_update(l);
}

void win_layout_set_render(struct win_layout *l, int win, win_render_handler_t render, void *userdata) {
	l->windows[win].render = render;
	l->windows[win].render_userdata = userdata;
}
void win_layout_set_layout(struct win_layout *l, int win, win_layout_handler_t layout, void *userdata) {
	l->windows[win].layout = layout;
	l->windows[win].layout_userdata = userdata;
}

void win_layout_enable(struct win_layout *l, int win, bool enabled) {
	if (l->windows[win].enabled == enabled)
		return;

	l->windows[win].enabled = enabled;

	win_layout_update(l);
}

void win_layout_resize(struct win_layout *l, int win, int width, int height) {
	struct win_state *w = &l->windows[win];

	if (w->width == width && w->height == height)
		return;

	w->width = width;
	w->height = height;

	win_layout_update(l);
}

void win_layout_draw_char(struct win_layout *l, int win, int x, int y, struct win_char ch) {
	struct win_state *w = &l->windows[win];

	if (!l->draw_char ||
	    x < 0 || y < 0 ||
	    x >= win_rect_width(w->chars_bounds) ||
	    y >= win_rect_height(w->chars_bounds))
		return;

	x += w->chars_bounds.x0;
	y += w->chars_bounds.y0;

	l->draw_char(x, y, ch, l->userdata);
}

static void win_render_window_border(struct win_layout *l, int win, struct win_rect area) {
	struct win_state *w = &l->windows[win];

	if (!l->draw_char)
		return;

	for (uint16_t y = area.y0; y < area.y1; y++) {
		for (uint16_t x = area.x0; x < area.x1; x++) {
			if (!win_rect_contains_point(w->chars_bounds, x, y)) {
				struct win_char ch = {
					.code = ' ',
					.bg = 0x1234,
					.fg = 0xfedc,
				};
				l->draw_char(x, y, ch, l->userdata);
			}
		}
	}
}

void win_layout_render(struct win_layout *l) {
	if (!l->draw_char)
		return;

	for (int i = 0; i < l->draw_count; i++) {
		struct win_state *w = &l->windows[l->draw_order[i]];

		struct win_rect area = win_rect_intersection(l->dirty, w->win_bounds);
		if (w->render) {
			struct win_rect redraw_rect = area;
			win_rect_translate(&redraw_rect, -w->chars_bounds.x0, -w->chars_bounds.y0);
			w->render(l, l->draw_order[i], redraw_rect, w->render_userdata);
		}

		if (win_rect_intersects(w->win_bounds, l->dirty)) {
			win_render_window_border(l, l->draw_order[i], area);
		}
	}
	if (l->commit)
		l->commit(l->userdata);

	l->dirty = (struct win_rect){ 0 };
}
