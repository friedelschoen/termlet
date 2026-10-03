#pragma once

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

struct byte_buffer {
	uint8_t *data;
	size_t len;
	size_t capacity;
};

struct compose_node {
	xkb_keysym_t keysym; /* 0 = root */

	struct compose_node *children;
	size_t child_count;
	size_t child_capacity;

	xkb_keysym_t result_sym;
	uint32_t utf8_offset;

	struct compose_node *order;
};

struct utf8_map {
	struct utf8_entry *entries;
	size_t capacity;
	size_t count;
};

void die(const char *message);
void die_errno(const char *what);


struct compose_node compose_compile(struct utf8_map *utf8_map, struct byte_buffer *utf8_buf, struct xkb_context *ctx, const char *locale);
void compose_free_node(struct compose_node *node);

size_t utf8_put(struct utf8_map *map, struct byte_buffer *buf, const char *seq, size_t seq_length);
void byte_buffer_append(struct byte_buffer *buf, const void *data, size_t len);
