#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEYMAP_MAX_CODE 255u

enum {
	/* table mask */
	KEYMAP_LSHIFT = 1 << 0,
	KEYMAP_RALT = 1 << 1,
	KEYMAP_CAPS = 1 << 2,
	KEYMAP_NUM = 1 << 3,

	/* control mask */
	KEYMAP_RSHIFT = 1 << 4,
	KEYMAP_LALT = 1 << 5,
	KEYMAP_LCTRL = 1 << 6,
	KEYMAP_RCTRL = 1 << 7,
	KEYMAP_LSUPER = 1 << 8,
	KEYMAP_RSUPER = 1 << 9,

	KEYMAP_SHIFT_MASK = KEYMAP_LSHIFT | KEYMAP_RSHIFT,
	KEYMAP_ALT_MASK = KEYMAP_LALT,
	KEYMAP_CTRL_MASK = KEYMAP_LCTRL | KEYMAP_RCTRL,
	KEYMAP_SUPER_MASK = KEYMAP_LSUPER | KEYMAP_RSUPER,

	/* all states that matter in keymap_lookup */
	/* NOTE: LSHIFT is used as a union for LSHIFT AND RSHIFT */
	KEYMAP_TABLE_MASK = KEYMAP_LSHIFT | KEYMAP_RALT | KEYMAP_CAPS | KEYMAP_NUM
};

enum keymap_compose_status {
	KEYMAP_COMPOSE_NOTHING = 0,
	KEYMAP_COMPOSE_COMPOSING,
	KEYMAP_COMPOSE_COMPOSED,
	KEYMAP_COMPOSE_CANCELLED,
};

typedef uint32_t keymap_compose_state_t;

struct keymap_compose_result {
	uint32_t keysym;
	const uint8_t *utf8;
	uint16_t utf8_len;
};

typedef uint16_t keymap_state_t; /* must fit all masks */

uint32_t keymap_lookup(keymap_state_t *state, uint32_t code, int pressed, const char **utf8);

int keymap_repeats(uint16_t evdev_code);

void keymap_compose_reset(keymap_compose_state_t *state);

enum keymap_compose_status keymap_compose_feed(keymap_compose_state_t *state,
                                               uint32_t *keysym,
                                               const char **utf8);

#ifdef __cplusplus
}
#endif
