#pragma once

#include <stdint.h>

struct keymap_key_value {
	uint32_t keysym;
	uint32_t utf8_offset;
};

struct compose_node {
	uint32_t keysym;
	uint32_t result_keysym;
	uint32_t utf8_offset;
	uint16_t child_offset;
	uint16_t child_count;
};

/*struct compose_edge {
    uint32_t keysym;
    uint32_t next_node;
};*/
