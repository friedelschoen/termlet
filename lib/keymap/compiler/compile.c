/*
 * keymap-keymap-compile.c
 *
 * Host-side compiler for Termlet keyboard maps.
 *
 * Compiles an XKB RMLVO keymap and a libxkbcommon Compose table into a
 * standalone C source file written to stdout.  The generated source has no
 * runtime dependency on libxkbcommon.
 *
 * Zephyr INPUT_KEY_* values are assumed to match Linux evdev KEY_* values;
 * the conventional XKB evdev keycode is evdev + 8.
 *
 * Build:
 *   cc -std=c11 -O2 -Wall -Wextra -Wpedantic \
 *      $(pkg-config --cflags xkbcommon) \
 *      keymap-keymap-compile.c -o keymap-keymap-compile \
 *      $(pkg-config --libs xkbcommon)
 *
 * Example:
 *   ./keymap-keymap-compile \
 *       --layout us --options compose:ralt --locale en_US.UTF-8 \
 *       > generated_keymap.c
 *
 * The generated C expects a fixed header (default: keymap.h) which
 * declares the keymap_key_* structs/enums/functions and constants used below.
 */

#include "../include/keymap.h"
#include "internal.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#define EVDEV_XKB_OFFSET 8u


struct options {
	const char *rules;
	const char *model;
	const char *layout;
	const char *variant;
	const char *xkb_options;
	const char *locale;
	const char *header;
	const char *int_header;
	uint32_t max_code;
};

struct key_value {
	xkb_keysym_t sym;
	uint32_t utf8_offset;
};

void die(const char *message) {
	fprintf(stderr, "error: %s\n", message);
	exit(EXIT_FAILURE);
}

void die_errno(const char *what) {
	fprintf(stderr, "error: %s: %s\n", what, strerror(errno));
	exit(EXIT_FAILURE);
}

static void print_usage(FILE *out, const char *argv0) {
	fprintf(out,
	        "usage: %s [options] > generated_keymap.c\n"
	        "\n"
	        "options:\n"
	        "  --rules NAME       XKB rules (default: evdev)\n"
	        "  --model NAME       XKB model (default: pc105)\n"
	        "  --layout NAME      XKB layout (default: us)\n"
	        "  --variant NAME     XKB variant\n"
	        "  --options LIST     XKB options, e.g. compose:ralt,caps:escape\n"
	        "  --locale LOCALE    Compose locale (default: LC_ALL/LC_CTYPE/LANG)\n"
	        "  --max-code N       largest evdev/Zephyr code (default: 255)\n"
	        "  --header FILE      generated source include (default: keymap.h)\n"
	        "  --int-header FILE  generated source internal include (default: keymap.h)\n"
	        "  -h, --help         show this help\n",
	        argv0);
}

static uint32_t parse_u32(const char *s, const char *option) {
	char *end = NULL;
	errno = 0;
	unsigned long value = strtoul(s, &end, 0);
	if (errno || !end || *end != '\0' || value > UINT32_MAX) {
		fprintf(stderr, "error: invalid %s value: %s\n", option, s);
		exit(EXIT_FAILURE);
	}
	return (uint32_t) value;
}

static const char *choose_locale(const char *explicit_locale) {
	if (explicit_locale && *explicit_locale)
		return explicit_locale;

	static const char *const vars[] = { "LC_ALL", "LC_CTYPE", "LANG" };
	for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); ++i) {
		const char *value = getenv(vars[i]);
		if (value && *value)
			return value;
	}
	return "C.UTF-8";
}

static struct options parse_options(int argc, char **argv) {
	struct options opts = {
		.rules = "evdev",
		.model = "pc105",
		.layout = "us",
		.variant = NULL,
		.xkb_options = NULL,
		.locale = NULL,
		.header = "keymap.h",
		.int_header = "keymap-int.h",
		.max_code = 255,
	};

	enum {
		OPT_RULES = 1000,
		OPT_MODEL,
		OPT_LAYOUT,
		OPT_VARIANT,
		OPT_OPTIONS,
		OPT_LOCALE,
		OPT_MAX_CODE,
		OPT_HEADER,
		OPT_INT_HEADER,
	};

	static const struct option long_options[] = {
		{ "rules", required_argument, NULL, OPT_RULES },
		{ "model", required_argument, NULL, OPT_MODEL },
		{ "layout", required_argument, NULL, OPT_LAYOUT },
		{ "variant", required_argument, NULL, OPT_VARIANT },
		{ "options", required_argument, NULL, OPT_OPTIONS },
		{ "locale", required_argument, NULL, OPT_LOCALE },
		{ "max-code", required_argument, NULL, OPT_MAX_CODE },
		{ "header", required_argument, NULL, OPT_HEADER },
		{ "int-header", required_argument, NULL, OPT_INT_HEADER },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};

	for (;;) {
		int c = getopt_long(argc, argv, "h", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
			case OPT_RULES:
				opts.rules = optarg;
				break;
			case OPT_MODEL:
				opts.model = optarg;
				break;
			case OPT_LAYOUT:
				opts.layout = optarg;
				break;
			case OPT_VARIANT:
				opts.variant = optarg;
				break;
			case OPT_OPTIONS:
				opts.xkb_options = optarg;
				break;
			case OPT_LOCALE:
				opts.locale = optarg;
				break;
			case OPT_MAX_CODE:
				opts.max_code = parse_u32(optarg, "--max-code");
				break;
			case OPT_HEADER:
				opts.header = optarg;
				break;
			case OPT_INT_HEADER:
				opts.int_header = optarg;
				break;
			case 'h':
				print_usage(stdout, argv[0]);
				exit(EXIT_SUCCESS);
			default:
				print_usage(stderr, argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	if (optind != argc) {
		fprintf(stderr, "error: unexpected positional argument: %s\n", argv[optind]);
		exit(EXIT_FAILURE);
	}

	if (opts.max_code > UINT16_MAX)
		die("--max-code must be in 0..65535");

	opts.locale = choose_locale(opts.locale);
	return opts;
}

static xkb_keycode_t keycode_by_name(struct xkb_keymap *keymap, const char *name) {
	xkb_keycode_t code = xkb_keymap_key_by_name(keymap, name);
	if (code == XKB_KEYCODE_INVALID) {
		fprintf(stderr, "error: keymap does not contain key %s\n", name);
		exit(EXIT_FAILURE);
	}
	return code;
}

static void tap(struct xkb_state *state, xkb_keycode_t keycode) {
	xkb_state_update_key(state, keycode, XKB_KEY_DOWN);
	xkb_state_update_key(state, keycode, XKB_KEY_UP);
}

static struct xkb_state *make_state(struct xkb_keymap *keymap,
                                    uint8_t bits,
                                    xkb_keycode_t lfsh,
                                    xkb_keycode_t ralt,
                                    xkb_keycode_t caps,
                                    xkb_keycode_t nmlk) {
	struct xkb_state *state = xkb_state_new(keymap);
	if (!state)
		die("xkb_state_new() failed");

	/* Lock keys first, then depressed modifiers. */
	if (bits & KEYMAP_CAPS)
		tap(state, caps);
	if (bits & KEYMAP_NUM)
		tap(state, nmlk);
	if (bits & KEYMAP_LSHIFT)
		xkb_state_update_key(state, lfsh, XKB_KEY_DOWN);
	if (bits & KEYMAP_RALT)
		xkb_state_update_key(state, ralt, XKB_KEY_DOWN);

	return state;
}

static struct key_value *compile_key_table(struct utf8_map *utf8_map, struct byte_buffer *utf8_buf, struct xkb_keymap *keymap, uint32_t max_code, uint8_t **repeat_out) {
	const size_t width = (size_t) max_code + 1;
	const size_t count = (KEYMAP_TABLE_MASK + 1) * width;
	char utf8[8];
	struct key_value *table = calloc(count, sizeof(*table));
	if (!table)
		die_errno("calloc key table");

	xkb_keycode_t lfsh = keycode_by_name(keymap, "LFSH");
	xkb_keycode_t ralt = keycode_by_name(keymap, "RALT");
	xkb_keycode_t caps = keycode_by_name(keymap, "CAPS");
	xkb_keycode_t nmlk = keycode_by_name(keymap, "NMLK");

	for (uint8_t bits = 0; bits <= KEYMAP_TABLE_MASK; ++bits) {
		struct xkb_state *state = make_state(keymap, bits, lfsh, ralt, caps, nmlk);

		for (uint32_t evdev = 0; evdev <= max_code; ++evdev) {
			xkb_keycode_t code = (xkb_keycode_t) (evdev + EVDEV_XKB_OFFSET);
			struct key_value *value = &table[(size_t) bits * width + evdev];
			value->sym = xkb_state_key_get_one_sym(state, code);

			uint32_t utf8_len = xkb_state_key_get_utf8(state, code, utf8, sizeof(utf8));
			value->utf8_offset = utf8_put(utf8_map, utf8_buf, utf8, utf8_len);
		}

		xkb_state_unref(state);
	}

	size_t repeat_bytes = (width + 7) / 8;
	uint8_t *repeat = calloc(repeat_bytes ? repeat_bytes : 1, 1);
	if (!repeat)
		die_errno("calloc repeat bitmap");

	for (uint32_t evdev = 0; evdev <= max_code; ++evdev) {
		xkb_keycode_t code = (xkb_keycode_t) (evdev + EVDEV_XKB_OFFSET);
		if (xkb_keymap_key_repeats(keymap, code))
			repeat[evdev >> 3] |= (uint8_t) (1u << (evdev & 7u));
	}

	*repeat_out = repeat;
	return table;
}

static void compose_print_node(struct compose_node *root) {
	uint32_t child_offset = 1;

	for (struct compose_node *node = root; node != NULL; node = node->order) {
		printf("    { %" PRIu32 ", %" PRIu32 ", %" PRIu32 ", %" PRIu32 ", %" PRIu16 " },\n",
		       node->keysym, node->result_sym, node->utf8_offset, child_offset, (uint16_t) node->child_count);

		child_offset += node->child_count;
	}
}

static void emit_source(struct byte_buffer *utf8_buf, const struct options *opts,
                        const struct key_value *key_table,
                        const uint8_t *repeat,
                        struct compose_node *compose_tree) {
	/* flatten_compose sorts children, hence takes a mutable tree. */
	const size_t width = (size_t) opts->max_code + 1;
	const size_t repeat_bytes = (width + 7) / 8;

	puts("/* Generated by keymap-keymap-compile. Do not edit. */");
	printf("#include \"%s\"\n", opts->header);
	printf("#include \"%s\"\n", opts->int_header);
	puts("");

	printf("const struct keymap_key_value keymap[KEYMAP_TABLE_MASK+1][KEYMAP_MAX_CODE + 1] = {\n");
	for (uint32_t state = 0; state <= KEYMAP_TABLE_MASK; state++) {
		printf("    [%" PRIu32 "] = {\n", state);
		for (uint32_t code = 0; code <= opts->max_code; ++code) {
			const struct key_value *value = &key_table[(size_t) state * width + code];
			if (value->sym == XKB_KEY_NoSymbol && value->utf8_offset == 0)
				continue;
			printf("        [%" PRIu32 "] = { %" PRIu32 ", %" PRIu32 " },\n", code, value->sym, value->utf8_offset);
		}
		puts("    },");
	}
	puts("};");
	puts("");

	puts("const uint8_t repeat_bitmap[] = {");
	for (size_t i = 0; i < repeat_bytes; i += 16) {
		fputs("    ", stdout);
		size_t end = i + 16 < repeat_bytes ? i + 16 : repeat_bytes;
		for (size_t j = i; j < end; ++j)
			printf("0x%02x,%s", repeat[j], j + 1 == end ? "" : " ");
		putchar('\n');
	}
	puts("};");
	puts("");

	puts("const struct compose_node compose_nodes[] = {");
	compose_print_node(compose_tree);
	puts("};");
	puts("");

	puts("const uint8_t keymap_utf8[] = {");
	if (utf8_buf->len == 0) {
		puts("    0x00,");
	} else {
		for (size_t i = 0; i < utf8_buf->len; i += 16) {
			fputs("   ", stdout);
			size_t end = i + 16 < utf8_buf->len ? i + 16 : utf8_buf->len;
			for (size_t j = i; j < end; ++j)
				printf(" 0x%02x,", utf8_buf->data[j]);
			putchar('\n');
		}
	}
	puts("};");
	puts("");
}

int main(int argc, char **argv) {
	struct options opts = parse_options(argc, argv);

	struct utf8_map utf8_map = { 0 };
	struct byte_buffer utf8_buf = { 0 };
	byte_buffer_append(&utf8_buf, "", 1);

	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!ctx)
		die("xkb_context_new() failed");

	const struct xkb_rule_names names = {
		.rules = opts.rules,
		.model = opts.model,
		.layout = opts.layout,
		.variant = opts.variant,
		.options = opts.xkb_options,
	};

	struct xkb_keymap *keymap = xkb_keymap_new_from_names(
	    ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap)
		die("xkb_keymap_new_from_names() failed");

	uint8_t *repeat = NULL;
	struct key_value *key_table = compile_key_table(&utf8_map, &utf8_buf, keymap, opts.max_code, &repeat);
	struct compose_node compose = compose_compile(&utf8_map, &utf8_buf, ctx, opts.locale);

	emit_source(&utf8_buf, &opts, key_table, repeat, &compose);

	fprintf(stderr,
	        "generated: layout=%s variant=%s options=%s locale=%s\n ",
	        opts.layout,
	        opts.variant ? opts.variant : "",
	        opts.xkb_options ? opts.xkb_options : "",
	        opts.locale);

	compose_free_node(&compose);
	free(key_table);
	free(repeat);
	free(utf8_buf.data);
	free(utf8_map.entries);
	xkb_keymap_unref(keymap);
	xkb_context_unref(ctx);
	return EXIT_SUCCESS;
}
