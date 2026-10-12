#pragma once

#include <stdbool.h>
#include <stdint.h>

struct win_key_event {
	uint32_t keysym;
	uint16_t modifiers;
	bool pressed;
	const char *utf8;
};

struct win_pointer_event {
	uint16_t x, y;
	uint32_t scroll, hscroll;
	int16_t dx, dy;
	int32_t dscroll, dhscroll;
};
