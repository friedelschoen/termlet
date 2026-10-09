#pragma once

#include <stdbool.h>
#include <stdint.h>

enum win_input_event_type {
	WIN_INPUT_KEY,
	WIN_INPUT_POINTER
};

struct win_input_key_event {
	enum win_input_event_type _type;

	uint32_t keysym;
	uint16_t modifiers;
	bool pressed;
	const char *utf8;
};

struct win_input_pointer_event {
	enum win_input_event_type _type;

	uint16_t x, y;
	uint32_t scroll, hscroll;
	int16_t dx, dy;
	int32_t dscroll, dhscroll;
};

union win_input_event {
	enum win_input_event_type type;
	struct win_input_key_event key;
	struct win_input_pointer_event pointer;
};
