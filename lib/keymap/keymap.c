#include "include/keymap.h"

#include "keymap-int.h"

#include <zephyr/dt-bindings/input/input-event-codes.h>

extern const struct keymap_key_value keymap[KEYMAP_TABLE_MASK + 1][KEYMAP_MAX_CODE + 1];
extern const uint8_t repeat_bitmap[];
extern const struct compose_node compose_nodes[];
extern const uint8_t keymap_utf8[];

static uint8_t table_state(keymap_state_t *state) {
	uint8_t result = *state & KEYMAP_TABLE_MASK;

	if (*state & KEYMAP_RSHIFT)
		result |= KEYMAP_LSHIFT;

	return result;
}

uint32_t keymap_lookup(keymap_state_t *state, uint32_t code, int pressed, const char **utf8) {
	if (code > KEYMAP_MAX_CODE)
		return 0;

	/*
	 * Resolve using the state BEFORE this key event.
	 */
	struct keymap_key_value k = keymap[table_state(state)][code];

	/*
	 * Then update physical keyboard state.
	 */
	switch (code) {
		case INPUT_KEY_LEFTCTRL:
			if (pressed)
				*state |= KEYMAP_LCTRL;
			else
				*state &= ~KEYMAP_LCTRL;
			break;
		case INPUT_KEY_RIGHTCTRL:
			if (pressed)
				*state |= KEYMAP_RCTRL;
			else
				*state &= ~KEYMAP_RCTRL;
			break;
		case INPUT_KEY_LEFTSHIFT:
			if (pressed)
				*state |= KEYMAP_LSHIFT;
			else
				*state &= ~KEYMAP_LSHIFT;
			break;

		case INPUT_KEY_RIGHTSHIFT:
			if (pressed)
				*state |= KEYMAP_RSHIFT;
			else
				*state &= ~KEYMAP_RSHIFT;
			break;

		case INPUT_KEY_RIGHTALT:
			if (pressed)
				*state |= KEYMAP_RALT;
			else
				*state &= ~KEYMAP_RALT;
			break;
		case INPUT_KEY_LEFTALT:
			if (pressed)
				*state |= KEYMAP_LALT;
			else
				*state &= ~KEYMAP_LALT;
			break;
		case INPUT_KEY_RIGHTMETA:
			if (pressed)
				*state |= KEYMAP_RSUPER;
			else
				*state &= ~KEYMAP_RSUPER;
			break;
		case INPUT_KEY_LEFTMETA:
			if (pressed)
				*state |= KEYMAP_LSUPER;
			else
				*state &= ~KEYMAP_LSUPER;
			break;
		case INPUT_KEY_CAPSLOCK:
			if (pressed)
				*state ^= KEYMAP_CAPS;
			break;

		case INPUT_KEY_NUMLOCK:
			if (pressed)
				*state ^= KEYMAP_NUM;
			break;
	}

	if (utf8)
		*utf8 = keymap_utf8 + k.utf8_offset;

	return k.keysym;
}

int keymap_repeats(uint16_t evdev_code) {
	if (evdev_code > KEYMAP_MAX_CODE)
		return 0;
	return (repeat_bitmap[evdev_code >> 3] & (1u << (evdev_code & 7u))) != 0;
}

void keymap_compose_reset(keymap_compose_state_t *state) {
	*state = 0;
}

static uint32_t compose_find_edge(uint32_t node_index, uint32_t keysym) {
	const struct compose_node *node = &compose_nodes[node_index];
	const uint32_t begin = node->child_offset;
	const uint32_t end = begin + node->child_count;

	uint32_t lo = begin;
	uint32_t hi = end;

	while (lo < hi) {
		uint32_t mid = lo + ((hi - lo) >> 1);

		if (compose_nodes[mid].keysym < keysym)
			lo = mid + 1;
		else
			hi = mid;
	}

	if (lo < end && compose_nodes[lo].keysym == keysym)
		return lo;

	return UINT32_MAX;
}

enum keymap_compose_status keymap_compose_feed(keymap_compose_state_t *state,
                                               uint32_t *keysym,
                                               const char **utf8) {
	const uint32_t previous = *state;
	const uint32_t next = compose_find_edge(previous, *keysym);
	if (next == UINT32_MAX) {
		keymap_compose_reset(state);
		return previous == 0 ? KEYMAP_COMPOSE_NOTHING : KEYMAP_COMPOSE_CANCELLED;
	}

	const struct compose_node *node = &compose_nodes[next];
	if (node->result_keysym != 0 || node->utf8_offset != 0) {
		*keysym = node->result_keysym;
		if (utf8)
			*utf8 = keymap_utf8 + node->utf8_offset;
		keymap_compose_reset(state);
		return KEYMAP_COMPOSE_COMPOSED;
	}

	*state = next;
	return KEYMAP_COMPOSE_COMPOSING;
}
