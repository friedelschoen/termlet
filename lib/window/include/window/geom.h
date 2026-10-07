#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

struct win_rect {
	uint16_t x0;
	uint16_t y0;
	uint16_t x1; /* exclusive */
	uint16_t y1; /* exclusive */
};

static inline uint16_t win_rect_width(struct win_rect r) {
	return r.x1 - r.x0;
}

static inline uint16_t win_rect_height(struct win_rect r) {
	return r.y1 - r.y0;
}

static inline bool win_rect_empty(struct win_rect r) {
	return r.x0 >= r.x1 || r.y0 >= r.y1;
}

static inline bool win_rect_contains_point(struct win_rect r, int x, int y) {
	return x >= r.x0 && x < r.x1 &&
	       y >= r.y0 && y < r.y1;
}

static inline bool win_rect_equals(struct win_rect r, struct win_rect other) {
	return other.x0 == r.x0 && other.x1 == r.x1 &&
	       other.y0 == r.y0 && other.y1 == r.y1;
}

static inline bool win_rect_contains_rect(struct win_rect r, struct win_rect other) {
	if (win_rect_empty(other))
		return true;

	return other.x0 >= r.x0 && other.x1 <= r.x1 &&
	       other.y0 >= r.y0 && other.y1 <= r.y1;
}

static inline bool win_rect_intersects(struct win_rect a, struct win_rect b) {
	return a.x0 < b.x1 && a.x1 > b.x0 &&
	       a.y0 < b.y1 && a.y1 > b.y0;
}

static inline struct win_rect win_rect_intersection(struct win_rect a, struct win_rect b) {
	struct win_rect r = {
		.x0 = MAX(a.x0, b.x0),
		.y0 = MAX(a.y0, b.y0),
		.x1 = MIN(a.x1, b.x1),
		.y1 = MIN(a.y1, b.y1),
	};

	if (win_rect_empty(r))
		return (struct win_rect) { 0 };

	return r;
}

static inline void win_rect_include_point(struct win_rect *r, uint16_t x, uint16_t y) {
	if (win_rect_empty(*r)) {
		*r = (struct win_rect) {
			.x0 = x,
			.y0 = y,
			.x1 = x + 1,
			.y1 = y + 1,
		};
		return;
	}

	r->x0 = MIN(r->x0, x);
	r->y0 = MIN(r->y0, y);
	r->x1 = MAX(r->x1, x + 1);
	r->y1 = MAX(r->y1, y + 1);
}

static inline void win_rect_include_rect(struct win_rect *r, struct win_rect other) {
	if (win_rect_empty(other))
		return;

	if (win_rect_empty(*r)) {
		*r = other;
		return;
	}

	r->x0 = MIN(r->x0, other.x0);
	r->y0 = MIN(r->y0, other.y0);
	r->x1 = MAX(r->x1, other.x1);
	r->y1 = MAX(r->y1, other.y1);
}

static inline void win_rect_translate(struct win_rect *r, uint16_t x, uint16_t y) {
	r->x0 += x;
	r->y0 += y;
	r->x1 += x;
	r->y1 += y;
}
