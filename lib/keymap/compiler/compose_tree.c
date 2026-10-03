#include "internal.h"

#include <stdlib.h>
#include <string.h>

static struct compose_node *compose_add_child(struct compose_node *node) {
	struct compose_node *child;

	if (node->child_count == node->child_capacity) {
		/* good practice to reallocate into a temporary variable... but if something fails it dies anyway */
		node->child_capacity = node->child_capacity ? node->child_capacity * 2 : 256;
		node->children = realloc(node->children, node->child_capacity * sizeof(*node->children));
		if (!node->children)
			die_errno("realloc");
	}

	if (node->child_count > UINT16_MAX)
		die("Compose tree has too many nodes");

	child = &node->children[node->child_count++];
	memset(child, 0, sizeof(*child));
	return child;
}

static struct compose_node *compose_get_child(struct compose_node *node, xkb_keysym_t keysym) {
	struct compose_node *child;

	/* maybe it's already defined */
	for (size_t i = 0; i < node->child_count; i++) {
		if (node->children[i].keysym == keysym)
			return &node->children[i];
	}

	/* otherwise add another */
	child = compose_add_child(node);
	child->keysym = keysym;
	return child;
}

static int compare_child(const void *ap, const void *bp) {
	const struct compose_node *a = ap;
	const struct compose_node *b = bp;
	if (a->keysym < b->keysym) return -1;
	if (a->keysym > b->keysym) return 1;
	return 0;
}

static void compose_order(struct compose_node *root) {
	struct compose_node *head = root;
	struct compose_node *tail = root;

	root->order = NULL;

	while (head) {
		struct compose_node *node = head;

		qsort(node->children, node->child_count, sizeof(*node->children), compare_child);
		for (size_t i = 0; i < node->child_count; i++) {
			struct compose_node *child = &node->children[i];

			child->order = NULL;
			tail->order = child;
			tail = child;
		}

		head = head->order;
	}
}

struct compose_node compose_compile(struct utf8_map *utf8_map, struct byte_buffer *utf8_buf, struct xkb_context *ctx, const char *locale) {
	struct compose_node root = { 0 };

	struct xkb_compose_table *table = xkb_compose_table_new_from_locale(
	    ctx, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
	if (!table) {
		fprintf(stderr,
		        "error: xkb_compose_table_new_from_locale(%s) failed; check locale/Compose data\n",
		        locale);
		exit(EXIT_FAILURE);
	}

	struct xkb_compose_table_iterator *it =
	    xkb_compose_table_iterator_new(table);
	if (!it)
		die("xkb_compose_table_iterator_new() failed");

	struct xkb_compose_table_entry *entry;
	while ((entry = xkb_compose_table_iterator_next(it)) != NULL) {
		size_t length = 0;
		struct compose_node *node = &root;
		const xkb_keysym_t *sequence =
		    xkb_compose_table_entry_sequence(entry, &length);

		/* get tree leaf, walking from root to specific node */
		for (size_t i = 0; i < length; ++i)
			node = compose_get_child(node, sequence[i]);

		node->result_sym = xkb_compose_table_entry_keysym(entry);
		const char *utf8 = xkb_compose_table_entry_utf8(entry);
		node->utf8_offset = utf8_put(utf8_map, utf8_buf, utf8, strlen(utf8));
	}

	xkb_compose_table_iterator_free(it);
	xkb_compose_table_unref(table);

	compose_order(&root);
	return root;
}

void compose_free_node(struct compose_node *node) {
	for (size_t i = 0; i < node->child_count; i++)
		compose_free_node(&node->children[i]);

	free(node->children);
}
