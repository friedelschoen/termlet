#include "internal.h"

#include <stdlib.h>
#include <string.h>

struct utf8_entry {
	uint32_t hash;
	uint32_t offset;
	uint32_t length;
	bool used;
};

static uint32_t fnv_1a(const char *data, size_t len) {
	uint32_t hash = 2166136261u;

	for (size_t i = 0; i < len; i++) {
		hash ^= (uint8_t) data[i];
		hash *= 16777619u;
	}

	/* 0 eventueel reserveren, al hoeft dat hier niet. */
	return hash ? hash : 1;
}

static bool utf8_equal(const struct utf8_entry *entry, const struct byte_buffer *buf, const char *seq, size_t len, uint32_t hash) {
	if (!entry->used)
		return false;

	if (entry->hash != hash || entry->length != len)
		return false;

	return memcmp(buf->data + entry->offset, seq, len) == 0;
}

void byte_buffer_append(struct byte_buffer *buf,
                        const void *data,
                        size_t len) {
	if (len == 0)
		return;

	if (buf->len > UINT32_MAX - len)
		die("Compose UTF-8 blob exceeds uint32_t offset range");

	size_t need = buf->len + len;
	if (need > buf->capacity) {
		size_t capacity = buf->capacity ? buf->capacity : 4096;
		while (capacity < need) {
			if (capacity > SIZE_MAX / 2)
				die("Compose UTF-8 blob capacity overflow");
			capacity *= 2;
		}
		buf->data = realloc(buf->data, capacity);
		if (!buf->data)
			die_errno("realloc");
		buf->capacity = capacity;
	}

	memcpy(buf->data + buf->len, data, len);
	buf->len += len;
}

static void utf8_map_grow(struct utf8_map *map) {
	size_t old_capacity = map->capacity;
	struct utf8_entry *old_entries = map->entries;

	size_t new_capacity = old_capacity ? old_capacity * 2 : 256;

	struct utf8_entry *entries =
	    calloc(new_capacity, sizeof(*entries));
	if (!entries)
		die_errno("calloc utf8 map");

	map->entries = entries;
	map->capacity = new_capacity;
	map->count = 0;

	for (size_t i = 0; i < old_capacity; i++) {
		const struct utf8_entry *old = &old_entries[i];

		if (!old->used)
			continue;

		size_t index = old->hash % new_capacity;

		while (entries[index].used)
			index = (index + 1) % new_capacity;

		entries[index] = *old;
		map->count++;
	}

	free(old_entries);
}

size_t utf8_put(struct utf8_map *map,
                struct byte_buffer *buf,
                const char *seq,
                size_t len) {
	if (!seq || len == 0)
		return 0;

	if (map->capacity == 0 ||
	    (map->count + 1) * 10 >= map->capacity * 7)
		utf8_map_grow(map);

	const uint32_t hash = fnv_1a(seq, len);
	size_t index = hash % map->capacity;

	for (;;) {
		struct utf8_entry *entry = &map->entries[index];

		if (!entry->used) {
			const size_t offset = buf->len;

			byte_buffer_append(buf, seq, len);
			byte_buffer_append(buf, "", 1);

			*entry = (struct utf8_entry){
				.hash = hash,
				.offset = offset,
				.length = len,
				.used = true,
			};

			map->count++;
			return offset;
		}

		if (utf8_equal(entry, buf, seq, len, hash))
			return entry->offset;

		index = (index + 1) % map->capacity;
	}
}
