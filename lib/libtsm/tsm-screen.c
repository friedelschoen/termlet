/*
 * libtsm - Screen Management
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Screen Management
 * This provides the abstracted screen management. It does not do any
 * terminal-emulation, instead it provides a resizable table of cells. You can
 * insert, remove and modify the cells freely.
 * A screen has always a fixed, but changeable, width and height. This defines
 * the number of columns and rows. The screen doesn't care for pixels, glyphs or
 * framebuffers. The screen only contains information about each cell.
 *
 * Screens are the logical model behind a real screen of a terminal emulator.
 * Users usually allocate a screen for each terminal-emulator they run. All they
 * have to do is render the screen onto their widget on each change and forward
 * any widget-events to the screen.
 *
 * The screen object already includes scrollback-buffers, selection support and
 * more. This simplifies terminal emulators a lot, but also prevents them from
 * accessing the real screen data. However, terminal emulators should have no
 * reason to access the data directly. The screen API should provide everything
 * they need.
 *
 * AGEING:
 * Each cell, line and screen has an "age" field. This field describes when it
 * was changed the last time. After drawing a screen, the current screen age is
 * returned. This allows users to skip drawing specific cells, if their
 * framebuffer was already drawn with a newer age than a given cell.
 * However, the screen-age might overflow. This is properly detected and causes
 * drawing functions to return "0" as age. Users must reset all their
 * framebuffer ages then. Otherwise, further drawing operations might
 * incorrectly skip cells.
 * Furthermore, if a cell has age "0", it means it _has_ to be drawn. No ageing
 * information is available.
 */

#include "libtsm.h"
#include "shl-llog.h"
#include "zephyr/logging/log.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
LOG_MODULE_REGISTER(libtsm, CONFIG_LIBTSM_LOG_LEVEL);

#define LLOG_SUBSYSTEM "tsm-screen"

struct tsm_line *tsm_linebuffer_get(struct tsm_linebuffer *b, unsigned int y)
{
	return &b->lines[b->order[y]];
}

static struct tsm_cell *get_cursor_cell(struct tsm_screen *con)
{
	unsigned int cur_x, cur_y;

	cur_x = con->cursor_x;
	if (cur_x >= con->size_x)
		cur_x = con->size_x - 1;

	cur_y = con->cursor_y;
	if (cur_y >= con->size_y)
		cur_y = con->size_y - 1;

	return &tsm_linebuffer_get(con->cur_lines, cur_y)->cells[cur_x];
}

static void move_cursor(struct tsm_screen *con, unsigned int x, unsigned int y)
{
	struct tsm_cell *c;

	/* if cursor is hidden, just move it */
	if (con->flags & TSM_SCREEN_HIDE_CURSOR) {
		con->cursor_x = x;
		con->cursor_y = y;
		return;
	}

	/* If cursor is visible, we have to mark the current and the new cell
	 * as changed by resetting their age. We skip it if the cursor-position
	 * didn't actually change. */

	if (con->cursor_x == x && con->cursor_y == y)
		return;

	c = get_cursor_cell(con);
	c->age = con->age_cnt;

	con->cursor_x = x;
	con->cursor_y = y;

	c = get_cursor_cell(con);
	c->age = con->age_cnt;
}

void screen_cell_init_generic(struct tsm_screen *con, struct tsm_cell *cell, struct tsm_screen_attr *attr)
{
	cell->ch = 0;
	cell->width = 1;
	cell->age = con->age_cnt;

	memcpy(&cell->attr, attr, sizeof(cell->attr));
}

void screen_cell_init(struct tsm_screen *con, struct tsm_cell *cell)
{
	screen_cell_init_generic(con, cell, &con->def_attr);
}

static int line_new(struct tsm_screen *con, struct tsm_line *line, unsigned int width)
{
	unsigned int i;

	if (!width)
		return -EINVAL;

	if (width > CONFIG_LIBTSM_MAX_WIDTH)
		width = CONFIG_LIBTSM_MAX_WIDTH;

	line->size = width;
	line->age = con->age_cnt;

	for (i = 0; i < width; ++i)
		screen_cell_init(con, &line->cells[i]);

	return 0;
}

static int line_resize(struct tsm_screen *con, struct tsm_line *line, unsigned int width)
{
	if (!line || !width)
		return -EINVAL;

	if (width > CONFIG_LIBTSM_MAX_WIDTH)
		width = CONFIG_LIBTSM_MAX_WIDTH;

	if (line->size < width) {
		while (line->size < width) {
			screen_cell_init(con, &line->cells[line->size]);
			++line->size;
		}
	}

	return 0;
}

static void screen_scroll_up(struct tsm_screen *con, unsigned int num)
{
	unsigned int top = con->margin_top;
	unsigned int bottom = con->margin_bottom;
	unsigned int count = bottom - top + 1;

	if (num > count)
		num = count;

	while (num--) {
		uint16_t recycled = con->cur_lines->order[top];

		memmove(&con->cur_lines->order[top], &con->cur_lines->order[top + 1],
				(bottom - top) * sizeof(con->cur_lines->order[0]));

		con->cur_lines->order[bottom] = recycled;

		struct tsm_line *line = &con->cur_lines->lines[recycled];

		for (unsigned int x = 0; x < con->size_x; x++)
			screen_cell_init(con, &line->cells[x]);

		line->size = con->size_x;
		line->age = con->age_cnt;
	}

	con->age = con->age_cnt;
}

static void screen_scroll_down(struct tsm_screen *con, unsigned int num)
{
	unsigned int top = con->margin_top;
	unsigned int bottom = con->margin_bottom;
	unsigned int count = bottom - top + 1;

	if (num > count)
		num = count;

	while (num--) {
		uint16_t recycled = con->cur_lines->order[bottom];

		memmove(&con->cur_lines->order[top + 1], &con->cur_lines->order[top],
				(bottom - top) * sizeof(con->cur_lines->order[0]));

		con->cur_lines->order[top] = recycled;

		struct tsm_line *line = &con->cur_lines->lines[recycled];

		for (unsigned int x = 0; x < con->size_x; x++)
			screen_cell_init(con, &line->cells[x]);

		line->size = con->size_x;
		line->age = con->age_cnt;
	}

	con->age = con->age_cnt;
}

static void screen_write(struct tsm_screen *con, unsigned int x, unsigned int y, tsm_symbol_t ch,
						 unsigned int len, const struct tsm_screen_attr *attr)
{
	struct tsm_line *line;
	unsigned int i;

	if (!len)
		return;

	if (x >= con->size_x || y >= con->size_y) {
		llog_warning(con, "writing beyond buffer boundary");
		return;
	}

	line = tsm_linebuffer_get(con->cur_lines, y);

	if ((con->flags & TSM_SCREEN_INSERT_MODE) && (int) x < ((int) con->size_x - len)) {
		line->age = con->age_cnt;
		memmove(&line->cells[x + len], &line->cells[x], sizeof(struct tsm_cell) * (con->size_x - len - x));
	}

	line->cells[x].age = con->age_cnt;
	line->cells[x].ch = ch;
	line->cells[x].width = len;
	memcpy(&line->cells[x].attr, attr, sizeof(*attr));

	for (i = 1; i < len && i + x < con->size_x; ++i) {
		line->cells[x + i].age = con->age_cnt;
		line->cells[x + i].width = 0;
	}
}

static void screen_erase_region(struct tsm_screen *con, unsigned int x_from, unsigned int y_from,
								unsigned int x_to, unsigned int y_to, bool protect)
{
	unsigned int to;
	struct tsm_line *line;

	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	if (y_to >= con->size_y)
		y_to = con->size_y - 1;
	if (x_to >= con->size_x)
		x_to = con->size_x - 1;

	for (; y_from <= y_to; ++y_from) {
		line = tsm_linebuffer_get(con->cur_lines, y_from);
		if (!line) {
			x_from = 0;
			continue;
		}

		if (y_from == y_to)
			to = x_to;
		else
			to = con->size_x - 1;
		for (; x_from <= to; ++x_from) {
			if (protect && line->cells[x_from].attr.protect)
				continue;

			screen_cell_init(con, &line->cells[x_from]);
		}
		x_from = 0;
	}
}

static inline unsigned int to_abs_x(struct tsm_screen *con, unsigned int x)
{
	return x;
}

static inline unsigned int to_abs_y(struct tsm_screen *con, unsigned int y)
{
	if (!(con->flags & TSM_SCREEN_REL_ORIGIN))
		return y;

	return con->margin_top + y;
}

SHL_EXPORT
int tsm_screen_new(struct tsm_screen *con)
{
	int ret;

	if (!con)
		return -EINVAL;

	memset(con, 0, sizeof(*con));
	con->age_cnt = 1;
	con->age = con->age_cnt;
	con->def_attr.fr = 255;
	con->def_attr.fg = 255;
	con->def_attr.fb = 255;

	ret = tsm_symbol_table_new(&con->sym_table);
	if (ret)
		goto err_free;

	for (unsigned int i = 0; i < CONFIG_LIBTSM_MAX_HEIGHT; i++)
		con->main_lines.order[i] = i;
	for (unsigned int i = 0; i < CONFIG_LIBTSM_MAX_HEIGHT; i++)
		con->alt_lines.order[i] = i;

	ret = tsm_screen_resize(con, 80, 24);
	if (ret)
		goto err_free;

	llog_debug(con, "new screen");

	return 0;

err_free:
	tsm_symbol_table_free(&con->sym_table);
	return ret;
}

SHL_EXPORT
void tsm_screen_free(struct tsm_screen *con)
{
	tsm_symbol_table_free(&con->sym_table);
}

void tsm_screen_set_opts(struct tsm_screen *scr, unsigned int opts)
{
	if (!scr || !opts)
		return;

	scr->opts |= opts;
}

void tsm_screen_reset_opts(struct tsm_screen *scr, unsigned int opts)
{
	if (!scr || !opts)
		return;

	scr->opts &= ~opts;
}

unsigned int tsm_screen_get_opts(struct tsm_screen *scr)
{
	if (!scr)
		return 0;

	return scr->opts;
}

SHL_EXPORT
unsigned int tsm_screen_get_width(struct tsm_screen *con)
{
	if (!con)
		return 0;

	return con->size_x;
}

SHL_EXPORT
unsigned int tsm_screen_get_height(struct tsm_screen *con)
{
	if (!con)
		return 0;

	return con->size_y;
}

SHL_EXPORT
int tsm_screen_resize(struct tsm_screen *con, unsigned int x, unsigned int y)
{
	unsigned int i, j, start, diff;
	int ret;

	if (!con || !x || !y)
		return -EINVAL;

	if (x > CONFIG_LIBTSM_MAX_WIDTH)
		x = CONFIG_LIBTSM_MAX_WIDTH;
	if (y > CONFIG_LIBTSM_MAX_HEIGHT)
		y = CONFIG_LIBTSM_MAX_HEIGHT;

	if (con->size_x == x && con->size_y == y)
		return 0;

	/*
	 * Grow number of initialized lines.
	 */
	if (y > con->main_lines.line_num) {
		unsigned int width = x > con->size_x ? x : con->size_x;

		while (con->main_lines.line_num < y) {
			unsigned int n = con->main_lines.line_num;

			struct tsm_line *main = &con->main_lines.lines[con->main_lines.order[n]];

			struct tsm_line *alt = &con->alt_lines.lines[con->alt_lines.order[n]];

			ret = line_new(con, main, width);
			if (ret)
				return ret;

			ret = line_new(con, alt, width);
			if (ret)
				return ret;

			con->main_lines.line_num++;
			con->alt_lines.line_num++;
		}
	}

	/*
	 * Grow all initialized lines horizontally.
	 */
	if (x > con->size_x) {
		for (i = 0; i < con->main_lines.line_num; i++) {
			ret = line_resize(con, &con->main_lines.lines[i], x);

			if (ret)
				return ret;

			ret = line_resize(con, &con->alt_lines.lines[i], x);

			if (ret)
				return ret;
		}
	}

	screen_inc_age(con);

	/*
	 * Clear expansion / padding area.
	 */
	start = x;
	if (x > con->size_x)
		start = con->size_x;

	for (j = 0; j < con->main_lines.line_num; j++) {
		struct tsm_line *main = &con->main_lines.lines[j];

		struct tsm_line *alt = &con->alt_lines.lines[j];

		i = 0;
		if (j < con->size_y)
			i = start;

		for (; i < main->size; i++) {
			screen_cell_init_generic(con, &main->cells[i], &con->def_attr_main);
		}

		i = 0;
		if (j < con->size_y)
			i = con->size_x;

		for (; i < x; i++)
			screen_cell_init(con, &alt->cells[i]);
	}

	/*
	 * xterm resets margins on resize.
	 */
	con->margin_top = 0;
	con->margin_bottom = con->size_y ? con->size_y - 1 : 0;

	/*
	 * Reset tabs.
	 */
	for (i = 0; i < x; i++)
		con->tab_ruler[i] = (i % 8) == 0;

	/*
	 * Update width before vertical scrolling.
	 */
	con->size_x = x;

	if (con->cursor_x >= con->size_x)
		move_cursor(con, con->size_x - 1, con->cursor_y);

	/*
	 * Shrink height by scrolling excess rows out.
	 */
	if (y < con->size_y) {
		diff = con->size_y - y;

		screen_scroll_up(con, diff);

		if (con->cursor_y >= diff)
			move_cursor(con, con->cursor_x, con->cursor_y - diff);
		else
			move_cursor(con, con->cursor_x, 0);
	}

	con->size_y = y;

	con->main_lines.line_num = y;
	con->alt_lines.line_num = y;

	con->margin_bottom = y - 1;

	if (con->cursor_y >= con->size_y)
		move_cursor(con, con->cursor_x, con->size_y - 1);

	return 0;
}

SHL_EXPORT
int tsm_screen_set_margins(struct tsm_screen *con, unsigned int top, unsigned int bottom)
{
	unsigned int upper, lower;

	if (!con)
		return -EINVAL;

	if (!top)
		top = 1;

	if (bottom <= top) {
		upper = 0;
		lower = con->size_y - 1;
	} else if (bottom > con->size_y) {
		upper = 0;
		lower = con->size_y - 1;
	} else {
		upper = top - 1;
		lower = bottom - 1;
	}

	con->margin_top = upper;
	con->margin_bottom = lower;
	return 0;
}

SHL_EXPORT
void tsm_screen_set_def_attr(struct tsm_screen *con, const struct tsm_screen_attr *attr)
{
	if (!con || !attr)
		return;
	memcpy(&con->def_attr, attr, sizeof(*attr));
}

SHL_EXPORT
void tsm_screen_reset(struct tsm_screen *con)
{
	unsigned int i;

	if (!con)
		return;

	screen_inc_age(con);
	con->age = con->age_cnt;

	con->flags = 0;
	con->margin_top = 0;
	con->margin_bottom = con->size_y - 1;
	con->cur_lines = &con->main_lines;

	for (i = 0; i < con->size_x; ++i) {
		if (i % 8 == 0)
			con->tab_ruler[i] = true;
		else
			con->tab_ruler[i] = false;
	}
}

SHL_EXPORT
void tsm_screen_set_flags(struct tsm_screen *con, unsigned int flags)
{
	unsigned int old;
	struct tsm_cell *c;

	if (!con || !flags)
		return;

	screen_inc_age(con);

	old = con->flags;
	con->flags |= flags;

	if (!(old & TSM_SCREEN_ALTERNATE) && (flags & TSM_SCREEN_ALTERNATE)) {
		con->age = con->age_cnt;
		con->cur_lines = &con->alt_lines;

		/* save attributes of main screen when we switch to alt screen */
		memcpy(&con->def_attr_main, &con->def_attr, sizeof(con->def_attr));
	}

	if (!(old & TSM_SCREEN_HIDE_CURSOR) && (flags & TSM_SCREEN_HIDE_CURSOR)) {
		c = get_cursor_cell(con);
		c->age = con->age_cnt;
	}

	if (!(old & TSM_SCREEN_INVERSE) && (flags & TSM_SCREEN_INVERSE))
		con->age = con->age_cnt;
}

SHL_EXPORT
void tsm_screen_reset_flags(struct tsm_screen *con, unsigned int flags)
{
	unsigned int old;
	struct tsm_cell *c;

	if (!con || !flags)
		return;

	screen_inc_age(con);

	old = con->flags;
	con->flags &= ~flags;

	if ((old & TSM_SCREEN_ALTERNATE) && (flags & TSM_SCREEN_ALTERNATE)) {
		con->age = con->age_cnt;
		con->cur_lines = &con->main_lines;
	}

	if ((old & TSM_SCREEN_HIDE_CURSOR) && (flags & TSM_SCREEN_HIDE_CURSOR)) {
		c = get_cursor_cell(con);
		c->age = con->age_cnt;
	}

	if ((old & TSM_SCREEN_INVERSE) && (flags & TSM_SCREEN_INVERSE))
		con->age = con->age_cnt;
}

SHL_EXPORT
unsigned int tsm_screen_get_flags(struct tsm_screen *con)
{
	if (!con)
		return 0;

	return con->flags;
}

SHL_EXPORT
unsigned int tsm_screen_get_cursor_x(struct tsm_screen *con)
{
	if (!con)
		return 0;

	return con->cursor_x;
}

SHL_EXPORT
unsigned int tsm_screen_get_cursor_y(struct tsm_screen *con)
{
	if (!con)
		return 0;

	return con->cursor_y;
}

SHL_EXPORT
void tsm_screen_set_tabstop(struct tsm_screen *con)
{
	if (!con || con->cursor_x >= con->size_x)
		return;

	con->tab_ruler[con->cursor_x] = true;
}

SHL_EXPORT
void tsm_screen_reset_tabstop(struct tsm_screen *con)
{
	if (!con || con->cursor_x >= con->size_x)
		return;

	con->tab_ruler[con->cursor_x] = false;
}

SHL_EXPORT
void tsm_screen_reset_all_tabstops(struct tsm_screen *con)
{
	unsigned int i;

	if (!con)
		return;

	for (i = 0; i < con->size_x; ++i)
		con->tab_ruler[i] = false;
}

SHL_EXPORT
void tsm_screen_write(struct tsm_screen *con, tsm_symbol_t ch, const struct tsm_screen_attr *attr)
{
	unsigned int last, len;

	if (!con)
		return;

	len = tsm_symbol_get_width(&con->sym_table, ch);
	if (!len)
		return;

	screen_inc_age(con);

	if (con->cursor_y <= con->margin_bottom || con->cursor_y >= con->size_y)
		last = con->margin_bottom;
	else
		last = con->size_y - 1;

	if (con->cursor_x >= con->size_x) {
		if (con->flags & TSM_SCREEN_AUTO_WRAP)
			move_cursor(con, 0, con->cursor_y + 1);
		else
			move_cursor(con, con->size_x - 1, con->cursor_y);
	}

	if (con->cursor_y > last) {
		move_cursor(con, con->cursor_x, last);
		screen_scroll_up(con, 1);
	}

	screen_write(con, con->cursor_x, con->cursor_y, ch, len, attr);
	move_cursor(con, con->cursor_x + len, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_newline(struct tsm_screen *con)
{
	if (!con)
		return;

	screen_inc_age(con);

	tsm_screen_move_down(con, 1, true);
	tsm_screen_move_line_home(con);
}

SHL_EXPORT
void tsm_screen_scroll_up(struct tsm_screen *con, unsigned int num)
{
	if (!con || !num)
		return;

	screen_inc_age(con);

	screen_scroll_up(con, num);
}

SHL_EXPORT
void tsm_screen_scroll_down(struct tsm_screen *con, unsigned int num)
{
	if (!con || !num)
		return;

	screen_inc_age(con);

	screen_scroll_down(con, num);
}

SHL_EXPORT
void tsm_screen_move_to(struct tsm_screen *con, unsigned int x, unsigned int y)
{
	unsigned int last;

	if (!con)
		return;

	screen_inc_age(con);

	if (con->flags & TSM_SCREEN_REL_ORIGIN)
		last = con->margin_bottom;
	else
		last = con->size_y - 1;

	x = to_abs_x(con, x);
	if (x >= con->size_x)
		x = con->size_x - 1;

	y = to_abs_y(con, y);
	if (y > last)
		y = last;

	move_cursor(con, x, y);
}

SHL_EXPORT
void tsm_screen_move_up(struct tsm_screen *con, unsigned int num, bool scroll)
{
	unsigned int diff, size;

	if (!con || !num)
		return;

	screen_inc_age(con);

	if (con->cursor_y >= con->margin_top)
		size = con->margin_top;
	else
		size = 0;

	diff = con->cursor_y - size;
	if (num > diff) {
		num -= diff;
		if (scroll)
			screen_scroll_down(con, num);
		move_cursor(con, con->cursor_x, size);
	} else {
		move_cursor(con, con->cursor_x, con->cursor_y - num);
	}
}

SHL_EXPORT
void tsm_screen_move_down(struct tsm_screen *con, unsigned int num, bool scroll)
{
	unsigned int diff, size;

	if (!con || !num)
		return;

	screen_inc_age(con);

	if (con->cursor_y <= con->margin_bottom)
		size = con->margin_bottom + 1;
	else
		size = con->size_y;

	diff = size - con->cursor_y - 1;
	if (num > diff) {
		num -= diff;
		if (scroll)
			screen_scroll_up(con, num);
		move_cursor(con, con->cursor_x, size - 1);
	} else {
		move_cursor(con, con->cursor_x, con->cursor_y + num);
	}
}

SHL_EXPORT
void tsm_screen_move_left(struct tsm_screen *con, unsigned int num)
{
	unsigned int x;

	if (!con || !num)
		return;

	screen_inc_age(con);

	if (num > con->size_x)
		num = con->size_x;

	x = con->cursor_x;
	if (x >= con->size_x)
		x = con->size_x - 1;

	if (num > x)
		move_cursor(con, 0, con->cursor_y);
	else
		move_cursor(con, x - num, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_move_right(struct tsm_screen *con, unsigned int num)
{
	if (!con || !num)
		return;

	screen_inc_age(con);

	if (num > con->size_x)
		num = con->size_x;

	if (num + con->cursor_x >= con->size_x)
		move_cursor(con, con->size_x - 1, con->cursor_y);
	else
		move_cursor(con, con->cursor_x + num, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_move_line_end(struct tsm_screen *con)
{
	if (!con)
		return;

	screen_inc_age(con);

	move_cursor(con, con->size_x - 1, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_move_line_home(struct tsm_screen *con)
{
	if (!con)
		return;

	screen_inc_age(con);

	move_cursor(con, 0, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_tab_right(struct tsm_screen *con, unsigned int num)
{
	unsigned int i, j, x;

	if (!con || !num)
		return;

	screen_inc_age(con);

	x = con->cursor_x;
	for (i = 0; i < num; ++i) {
		for (j = x + 1; j < con->size_x; ++j) {
			if (con->tab_ruler[j])
				break;
		}

		x = j;
		if (x + 1 >= con->size_x)
			break;
	}

	/* tabs never cause pending new-lines */
	if (x >= con->size_x)
		x = con->size_x - 1;

	move_cursor(con, x, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_tab_left(struct tsm_screen *con, unsigned int num)
{
	unsigned int i, x;
	int j;

	if (!con || !num)
		return;

	screen_inc_age(con);

	x = con->cursor_x;
	for (i = 0; i < num; ++i) {
		for (j = x - 1; j > 0; --j) {
			if (con->tab_ruler[j])
				break;
		}

		if (j <= 0) {
			x = 0;
			break;
		}
		x = j;
	}

	move_cursor(con, x, con->cursor_y);
}

SHL_EXPORT
void tsm_screen_insert_lines(struct tsm_screen *con, unsigned int num)
{
	unsigned int max;

	if (!con || !num)
		return;

	if (con->cursor_y < con->margin_top || con->cursor_y > con->margin_bottom)
		return;

	screen_inc_age(con);
	con->age = con->age_cnt;

	max = con->margin_bottom - con->cursor_y + 1;
	if (num > max)
		num = max;

	struct tsm_linebuffer *buf = con->cur_lines;

	while (num--) {
		/*
		 * Recycle the last line in the affected region.
		 */
		uint16_t recycled = buf->order[con->margin_bottom];

		/*
		 * Shift cursor_y .. margin_bottom-1 down by one.
		 */
		memmove(&buf->order[con->cursor_y + 1], &buf->order[con->cursor_y],
				(con->margin_bottom - con->cursor_y) * sizeof(buf->order[0]));

		buf->order[con->cursor_y] = recycled;

		struct tsm_line *line = &buf->lines[recycled];

		for (unsigned int x = 0; x < con->size_x; ++x)
			screen_cell_init(con, &line->cells[x]);

		line->age = con->age_cnt;
	}

	con->cursor_x = 0;
}

SHL_EXPORT
void tsm_screen_delete_lines(struct tsm_screen *con, unsigned int num)
{
	unsigned int max;

	if (!con || !num)
		return;

	if (con->cursor_y < con->margin_top || con->cursor_y > con->margin_bottom)
		return;

	screen_inc_age(con);
	con->age = con->age_cnt;

	max = con->margin_bottom - con->cursor_y + 1;
	if (num > max)
		num = max;

	struct tsm_linebuffer *buf = con->cur_lines;

	while (num--) {
		/*
		 * Recycle the first line in the affected region.
		 */
		uint16_t recycled = buf->order[con->cursor_y];

		/*
		 * Shift cursor_y+1 .. margin_bottom up by one.
		 */
		memmove(&buf->order[con->cursor_y], &buf->order[con->cursor_y + 1],
				(con->margin_bottom - con->cursor_y) * sizeof(buf->order[0]));

		buf->order[con->margin_bottom] = recycled;

		struct tsm_line *line = &buf->lines[recycled];

		for (unsigned int x = 0; x < con->size_x; ++x)
			screen_cell_init(con, &line->cells[x]);

		line->age = con->age_cnt;
	}

	con->cursor_x = 0;
}

SHL_EXPORT
void tsm_screen_insert_chars(struct tsm_screen *con, unsigned int num)
{
	struct tsm_cell *cells;
	unsigned int max, mv, i;

	if (!con || !num || !con->size_y || !con->size_x)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	if (con->cursor_x >= con->size_x)
		con->cursor_x = con->size_x - 1;
	if (con->cursor_y >= con->size_y)
		con->cursor_y = con->size_y - 1;

	max = con->size_x - con->cursor_x;
	if (num > max)
		num = max;
	mv = max - num;

	cells = tsm_linebuffer_get(con->cur_lines, con->cursor_y)->cells;
	if (mv)
		memmove(&cells[con->cursor_x + num], &cells[con->cursor_x], mv * sizeof(*cells));

	for (i = 0; i < num; ++i)
		screen_cell_init(con, &cells[con->cursor_x + i]);
}

SHL_EXPORT
void tsm_screen_delete_chars(struct tsm_screen *con, unsigned int num)
{
	struct tsm_cell *cells;
	unsigned int max, mv, i;

	if (!con || !num || !con->size_y || !con->size_x)
		return;

	screen_inc_age(con);
	/* TODO: more sophisticated ageing */
	con->age = con->age_cnt;

	if (con->cursor_x >= con->size_x)
		con->cursor_x = con->size_x - 1;
	if (con->cursor_y >= con->size_y)
		con->cursor_y = con->size_y - 1;

	max = con->size_x - con->cursor_x;
	if (num > max)
		num = max;
	mv = max - num;

	cells = tsm_linebuffer_get(con->cur_lines, con->cursor_y)->cells;
	if (mv)
		memmove(&cells[con->cursor_x], &cells[con->cursor_x + num], mv * sizeof(*cells));

	for (i = 0; i < num; ++i)
		screen_cell_init(con, &cells[con->cursor_x + mv + i]);
}

SHL_EXPORT
void tsm_screen_erase_cursor(struct tsm_screen *con)
{
	unsigned int x;

	if (!con)
		return;

	screen_inc_age(con);

	if (con->cursor_x >= con->size_x)
		x = con->size_x - 1;
	else
		x = con->cursor_x;

	screen_erase_region(con, x, con->cursor_y, x, con->cursor_y, false);
}

SHL_EXPORT
void tsm_screen_erase_chars(struct tsm_screen *con, unsigned int num)
{
	unsigned int x;

	if (!con || !num)
		return;

	screen_inc_age(con);

	if (con->cursor_x >= con->size_x)
		x = con->size_x - 1;
	else
		x = con->cursor_x;

	screen_erase_region(con, x, con->cursor_y, x + num - 1, con->cursor_y, false);
}

SHL_EXPORT
void tsm_screen_erase_cursor_to_end(struct tsm_screen *con, bool protect)
{
	unsigned int x;

	if (!con)
		return;

	screen_inc_age(con);

	if (con->cursor_x >= con->size_x)
		x = con->size_x - 1;
	else
		x = con->cursor_x;

	screen_erase_region(con, x, con->cursor_y, con->size_x - 1, con->cursor_y, protect);
}

SHL_EXPORT
void tsm_screen_erase_home_to_cursor(struct tsm_screen *con, bool protect)
{
	if (!con)
		return;

	screen_inc_age(con);

	screen_erase_region(con, 0, con->cursor_y, con->cursor_x, con->cursor_y, protect);
}

SHL_EXPORT
void tsm_screen_erase_current_line(struct tsm_screen *con, bool protect)
{
	if (!con)
		return;

	screen_inc_age(con);

	screen_erase_region(con, 0, con->cursor_y, con->size_x - 1, con->cursor_y, protect);
}

SHL_EXPORT
void tsm_screen_erase_screen_to_cursor(struct tsm_screen *con, bool protect)
{
	if (!con)
		return;

	screen_inc_age(con);

	screen_erase_region(con, 0, 0, con->cursor_x, con->cursor_y, protect);
}

SHL_EXPORT
void tsm_screen_erase_cursor_to_screen(struct tsm_screen *con, bool protect)
{
	unsigned int x;

	if (!con)
		return;

	screen_inc_age(con);

	if (con->cursor_x >= con->size_x)
		x = con->size_x - 1;
	else
		x = con->cursor_x;

	screen_erase_region(con, x, con->cursor_y, con->size_x - 1, con->size_y - 1, protect);
}

SHL_EXPORT
void tsm_screen_erase_screen(struct tsm_screen *con, bool protect)
{
	if (!con)
		return;

	screen_inc_age(con);

	screen_erase_region(con, 0, 0, con->size_x - 1, con->size_y - 1, protect);
}
