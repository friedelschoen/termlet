/*
 * TSM - Main Header
 *
 * Copyright (c) 2018 Aetf <aetf@unlimitedcodeworks.xyz>
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

#ifndef TSM_LIBTSM_H
#define TSM_LIBTSM_H

#include <inttypes.h>
#include <shl-htable.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @mainpage
 *
 * TSM is a Terminal-emulator State Machine. It implements all common DEC-VT100
 * to DEC-VT520 control codes and features. A state-machine is used to parse TTY
 * input and saved in a virtual screen. TSM does not provide any rendering,
 * glyph/font handling or anything more advanced. TSM is just a simple
 * state-machine for control-codes handling.
 * The main use-case for TSM are terminal-emulators. TSM has no dependencies
 * other than an ISO-C99 compiler and C-library. Any terminal emulator for any
 * window-environment or rendering-pipline can make use of TSM. However, TSM can
 * also be used for control-code validation, TTY-screen-capturing or other
 * advanced users of terminal escape-sequences.
 */

/* max CSI arguments */
#define CSI_ARG_MAX 16

/* max length of an OSC code */
#define OSC_MAX_LEN 128

/* terminal flags */
#define TSM_VTE_FLAG_CURSOR_KEY_MODE 0x00000001 /* DEC cursor key mode */
#define TSM_VTE_FLAG_KEYPAD_APPLICATION_MODE                                                                 \
	0x00000002 /* DEC keypad application mode; TODO: toggle on numlock? */
#define TSM_VTE_FLAG_LINE_FEED_NEW_LINE_MODE 0x00000004 /* DEC line-feed/new-line mode */
#define TSM_VTE_FLAG_8BIT_MODE 0x00000008             /* Disable UTF-8 mode and enable 8bit compatible mode */
#define TSM_VTE_FLAG_7BIT_MODE 0x00000010             /* Disable 8bit mode and use 7bit compatible mode */
#define TSM_VTE_FLAG_USE_C1 0x00000020                /* Explicitly use 8bit C1 codes; TODO: implement */
#define TSM_VTE_FLAG_KEYBOARD_ACTION_MODE 0x00000040  /* Disable keyboard; TODO: implement? */
#define TSM_VTE_FLAG_INSERT_REPLACE_MODE 0x00000080   /* Enable insert mode */
#define TSM_VTE_FLAG_SEND_RECEIVE_MODE 0x00000100     /* Disable local echo */
#define TSM_VTE_FLAG_TEXT_CURSOR_MODE 0x00000200      /* Show cursor */
#define TSM_VTE_FLAG_INVERSE_SCREEN_MODE 0x00000400   /* Inverse colors */
#define TSM_VTE_FLAG_ORIGIN_MODE 0x00000800           /* Relative origin for cursor */
#define TSM_VTE_FLAG_AUTO_WRAP_MODE 0x00001000        /* Auto line wrap mode */
#define TSM_VTE_FLAG_AUTO_REPEAT_MODE 0x00002000      /* Auto repeat key press; TODO: implement */
#define TSM_VTE_FLAG_NATIONAL_CHARSET_MODE 0x00004000 /* Send keys from nation charsets; TODO: implement */
#define TSM_VTE_FLAG_BACKGROUND_COLOR_ERASE_MODE 0x00008000 /* Set background color on erase (bce) */
#define TSM_VTE_FLAG_PREPEND_ESCAPE 0x00010000              /* Prepend escape character to next output */
#define TSM_VTE_FLAG_TITE_INHIBIT_MODE 0x00020000           /* Prevent switching to alternate screen buffer */

/* keep in sync with shl_xkb_mods */
enum tsm_vte_modifier {
	TSM_SHIFT_MASK = (1 << 0),
	TSM_LOCK_MASK = (1 << 1),
	TSM_CONTROL_MASK = (1 << 2),
	TSM_ALT_MASK = (1 << 3),
	TSM_LOGO_MASK = (1 << 4),
};

/* keep in sync with TSM_INPUT_INVALID */
#define TSM_VTE_INVALID 0xffffffff

enum tsm_vte_color {
	TSM_COLOR_BLACK,
	TSM_COLOR_RED,
	TSM_COLOR_GREEN,
	TSM_COLOR_YELLOW,
	TSM_COLOR_BLUE,
	TSM_COLOR_MAGENTA,
	TSM_COLOR_CYAN,
	TSM_COLOR_LIGHT_GREY,
	TSM_COLOR_DARK_GREY,
	TSM_COLOR_LIGHT_RED,
	TSM_COLOR_LIGHT_GREEN,
	TSM_COLOR_LIGHT_YELLOW,
	TSM_COLOR_LIGHT_BLUE,
	TSM_COLOR_LIGHT_MAGENTA,
	TSM_COLOR_LIGHT_CYAN,
	TSM_COLOR_WHITE,

	TSM_COLOR_FOREGROUND,
	TSM_COLOR_BACKGROUND,

	TSM_COLOR_NUM
};

/**
 * Mouse Tracking
 *
 * Reference:
 *
 * https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h2-Mouse-Tracking
 *
 * The application running in the terminal can request a mouse tracking mode
 * and it can configure the event type (send position on click only or on click
 * and on mouse movement). The terminal will then send the position
 * of the mouse cursor as requested by the application.
 *
 * Since libtsm doesn't know anything about the UI or the mouse this can only
 * work if the terminal emulator built on top of libtsm cooperates.
 *
 * To implement mouse tracking a terminal emulator must first set a mouse
 * callback with tsm_vte_set_mouse_cb. This callback will be called whenever the
 * mouse mode changes in a way that is relevant to the terminal. It tells the
 * terminal if it needs to pass mouse events on click, on move, or not at all.
 *
 * To pass the mouse events the terminal needs to call tsm_vte_handle_mouse with
 * all parameters filled appropriately. The function will then take care of
 * sending the mouse events to the application in the correct encoding and
 * it will also discard events that are duplicate or unnecessary.
 */

/* control sequence codes sent be the application */
#define TSM_VTE_MOUSE_MODE_X10                                                                               \
	9 /* legacy mode (only cell mode, only on mouse click and x and y can be 223 max) */
#define TSM_VTE_MOUSE_EVENT_BTN 1002  /* sends position on mouse click only */
#define TSM_VTE_MOUSE_EVENT_ANY 1003  /* sends position on mouse click and mouse move */
#define TSM_VTE_MOUSE_MODE_SGR 1006   /* modern mode that allows unlimited x and y coordinates */
#define TSM_VTE_MOUSE_MODE_PIXEL 1016 /* sends pixel coordinates instead of cell coordinates */

enum tsm_mouse_track_mode {
	TSM_MOUSE_TRACK_DISABLE = 0,                   /* don't track mouse events */
	TSM_MOUSE_TRACK_BTN = TSM_VTE_MOUSE_EVENT_BTN, /* call tsm_vte_handle_mouse only for mouse clicks */
	TSM_MOUSE_TRACK_ANY =
		TSM_VTE_MOUSE_EVENT_ANY /* call tsm_vte_handle_mouse for mouse clicks and mouse movement */
};

/* mouse buttons to be passed to tsm_vte_handle_mouse */
#define TSM_MOUSE_BUTTON_LEFT 0
#define TSM_MOUSE_BUTTON_MIDDLE 1
#define TSM_MOUSE_BUTTON_RIGHT 2
#define TSM_MOUSE_BUTTON_WHEEL_UP 4
#define TSM_MOUSE_BUTTON_WHEEL_DOWN 5

/* modifier keys to be passed to tsm_vte_handle_mouse (can be combined with OR) */
#define TSM_MOUSE_MODIFIER_SHIFT 4
#define TSM_MOUSE_MODIFIER_META 8
#define TSM_MOUSE_MODIFIER_CTRL 16

/* events to be passed to tsm_vte_handle_mouse */
#define TSM_MOUSE_EVENT_PRESSED 1
#define TSM_MOUSE_EVENT_RELEASED 2
#define TSM_MOUSE_EVENT_MOVED 4

enum tsm_palette {
	TSM_PALETTE_SOLARIZED,
	TSM_PALETTE_SOLARIZED_LIGHT,
	TSM_PALETTE_SOLARIZED_DARK,
	TSM_PALETTE_SOFT_BLACK,
	TSM_PALETTE_BASE16_DARK,
	TSM_PALETTE_BASE16_LIGHT,
	TSM_PALETTE_VGA,
	TSM_PALETTE_LEGACY
};

/**
 * @defgroup misc Miscellaneous Definitions
 * Miscellaneous definitions
 *
 * This section contains several miscellaneous definitions of small helpers and
 * constants. These are shared between other parts of the API and have common
 * semantics/syntax.
 *
 * @{
 */

/* symbols */

struct tsm_symbol_table {
	unsigned long ref;
	uint32_t next_id;
	struct shl_array *index;
	struct shl_htable symbols;
};

typedef uint32_t tsm_symbol_t;

extern const tsm_symbol_t tsm_symbol_default;

int tsm_symbol_table_new(struct tsm_symbol_table *out);
void tsm_symbol_table_free(struct tsm_symbol_table *tbl);

tsm_symbol_t tsm_symbol_make(uint32_t ucs4);
tsm_symbol_t tsm_symbol_append(struct tsm_symbol_table *tbl, tsm_symbol_t sym, uint32_t ucs4);
const uint32_t *tsm_symbol_get(struct tsm_symbol_table *tbl, tsm_symbol_t *sym, size_t *size);
unsigned int tsm_symbol_get_width(struct tsm_symbol_table *tbl, tsm_symbol_t sym);

/* vte */

struct tsm_screen;
struct tsm_vte;

typedef void (*tsm_vte_write_cb)(struct tsm_vte *vte, const char *u8, size_t len, void *data);

typedef void (*tsm_vte_osc_cb)(struct tsm_vte *vte, const char *u8, size_t len, void *data);

typedef void (*tsm_vte_mouse_cb)(struct tsm_vte *vte, enum tsm_mouse_track_mode track_mode, bool track_pixels,
								 void *data);

int tsm_vte_new(struct tsm_vte *out, struct tsm_screen *con, tsm_vte_write_cb write_cb, void *data);

void tsm_vte_set_osc_cb(struct tsm_vte *vte, tsm_vte_osc_cb osc_cb, void *osc_data);
void tsm_vte_set_mouse_cb(struct tsm_vte *vte, tsm_vte_mouse_cb mouse_cb, void *mouse_data);

/**
 * @defgroup symbols Unicode Helpers
 * Unicode helpers
 *
 * Unicode uses 32bit types to uniquely represent symbols. However, combining
 * characters allow modifications of such symbols but require additional space.
 * To avoid passing around allocated strings, TSM provides a symbol-table which
 * can store combining-characters with their base-symbol to create a new symbol.
 * This way, only the symbol-identifiers have to be passed around (which are
 * simple integers). No string allocation is needed by the API user.
 *
 * The symbol table is currently not exported. Once the API is fixed, we will
 * provide it to outside users.
 *
 * Additionally, this contains some general UTF8/UCS4 helpers.
 *
 * @{
 */

/* UCS4 helpers */

#define TSM_UCS4_MAX_BITS 31
#define TSM_UCS4_MAX ((1UL << TSM_UCS4_MAX_BITS) - 1UL)
#define TSM_UCS4_INVALID (TSM_UCS4_MAX + 1)
#define TSM_UCS4_REPLACEMENT (0xfffdUL)

/* ucs4 to utf8 converter */

unsigned int tsm_ucs4_get_width(uint32_t ucs4);
size_t tsm_ucs4_to_utf8(uint32_t ucs4, char *out);
char *tsm_ucs4_to_utf8_alloc(const uint32_t *ucs4, size_t len, size_t *len_out);

/** @} */

/**
 * @defgroup screen Terminal Screens
 * Virtual terminal-screen implementation
 *
 * A TSM screen respresents the real screen of a terminal/application. It does
 * not render anything, but only provides a table of cells. Each cell contains
 * the stored symbol, attributes and more. Applications iterate a screen to
 * render each cell on their framebuffer.
 *
 * Screens provide all features that are expected from terminals. They include
 * scroll-back buffers, alternate screens, cursor positions and selection
 * support. Thus, it needs event-input from applications to drive these
 * features. Most of them are optional, though.
 *
 * @{
 */

typedef uint_fast32_t tsm_age_t;

#define TSM_SCREEN_INSERT_MODE 0x01
#define TSM_SCREEN_AUTO_WRAP 0x02
#define TSM_SCREEN_REL_ORIGIN 0x04
#define TSM_SCREEN_INVERSE 0x08
#define TSM_SCREEN_HIDE_CURSOR 0x10
#define TSM_SCREEN_FIXED_POS 0x20
#define TSM_SCREEN_ALTERNATE 0x40

#define SHL_EXPORT __attribute__((visibility("default")))

/* max combined-symbol length */
#define TSM_UCS4_MAXLEN 10

/* available character sets */

typedef tsm_symbol_t tsm_vte_charset[96];

extern tsm_vte_charset tsm_vte_unicode_lower;
extern tsm_vte_charset tsm_vte_unicode_upper;
extern tsm_vte_charset tsm_vte_dec_supplemental_graphics;
extern tsm_vte_charset tsm_vte_dec_special_graphics;
struct tsm_screen_attr {
	int8_t fccode;              /* foreground color code or <0 for rgb */
	int8_t bccode;              /* background color code or <0 for rgb */
	uint8_t fr;                 /* foreground red */
	uint8_t fg;                 /* foreground green */
	uint8_t fb;                 /* foreground blue */
	uint8_t br;                 /* background red */
	uint8_t bg;                 /* background green */
	uint8_t bb;                 /* background blue */
	unsigned int bold : 1;      /* bold character */
	unsigned int italic : 1;    /* italics character */
	unsigned int underline : 1; /* underlined character */
	unsigned int inverse : 1;   /* inverse colors */
	unsigned int protect : 1;   /* cannot be erased */
	unsigned int blink : 1;     /* blinking character */
};

/* TSM screen */

struct tsm_cell {
	tsm_symbol_t ch;             /* stored character */
	unsigned int width;          /* character width */
	struct tsm_screen_attr attr; /* cell attributes */
	tsm_age_t age;               /* age of the single cell */
};

struct tsm_line {
	unsigned int size;                              /* real width */
	struct tsm_cell cells[CONFIG_LIBTSM_MAX_WIDTH]; /* actuall cells */
	tsm_age_t age;                                  /* age of the whole line */
};

struct tsm_selection_pos {
	unsigned int x;
	int y;
};

struct tsm_linebuffer {
	struct tsm_line lines[CONFIG_LIBTSM_MAX_HEIGHT];
	uint16_t order[CONFIG_LIBTSM_MAX_HEIGHT];
	unsigned int line_num;
};

struct tsm_line *tsm_linebuffer_get(struct tsm_linebuffer *b, unsigned int index);

struct tsm_screen {
	unsigned int opts;
	unsigned int flags;
	struct tsm_symbol_table sym_table;

	/* default attributes for new cells */
	struct tsm_screen_attr def_attr;

	/* save default attributes of main screen here when we switch to alt screen
	 * on resize of the alt screen we need to init the new cells of the main
	 * screen with these attributes and not the ones of the alt screen */
	struct tsm_screen_attr def_attr_main;

	/* ageing */
	tsm_age_t age_cnt;          /* current age counter */
	unsigned int age_reset : 1; /* age-overflow flag */

	/* current buffer */
	unsigned int size_x;              /* width of screen */
	unsigned int size_y;              /* height of screen */
	unsigned int margin_top;          /* top-margin index */
	unsigned int margin_bottom;       /* bottom-margin index */
	struct tsm_linebuffer *cur_lines; /* active lines; copy of main/alt */
	struct tsm_linebuffer main_lines; /* real main lines */
	struct tsm_linebuffer alt_lines;  /* real alternative lines */
	tsm_age_t age;                    /* whole screen age */

	/* cursor: positions are always in-bound, but cursor_x might be
	 * bigger than size_x if new-line is pending */
	unsigned int cursor_x; /* current cursor x-pos */
	unsigned int cursor_y; /* current cursor y-pos */

	/* tab ruler */
	bool tab_ruler[CONFIG_LIBTSM_MAX_WIDTH]; /* tab-flag for all cells of one row */

	/* selection */
	bool sel_active;
	struct tsm_selection_pos sel_start;
	struct tsm_selection_pos sel_end;
};

struct tsm_vte_saved_state {
	unsigned int cursor_x;
	unsigned int cursor_y;
	struct tsm_screen_attr cattr;
	tsm_vte_charset **gl;
	tsm_vte_charset **gr;
	bool wrap_mode;
	bool origin_mode;
};

struct tsm_utf8_mach {
	int state;
	uint32_t ch;
};

struct tsm_vte {
	struct tsm_screen *con;
	tsm_vte_write_cb write_cb;
	void *data;
	enum tsm_palette palette_name;
	bool backspace_sends_delete;

	struct tsm_utf8_mach mach;
	unsigned long parse_cnt;

	unsigned int state;
	unsigned int csi_argc;
	int csi_argv[CSI_ARG_MAX];
	unsigned int csi_flags;

	tsm_vte_osc_cb osc_cb;
	void *osc_data;
	unsigned int osc_len;
	char osc_arg[OSC_MAX_LEN];

	tsm_vte_mouse_cb mouse_cb;
	void *mouse_data;
	unsigned int mouse_mode;
	unsigned int mouse_event;
	unsigned int mouse_last_col;
	unsigned int mouse_last_row;

	struct tsm_screen_attr def_attr;
	struct tsm_screen_attr cattr;
	unsigned int flags;

	tsm_vte_charset **gl;
	tsm_vte_charset **gr;
	tsm_vte_charset **glt;
	tsm_vte_charset **grt;
	tsm_vte_charset *g0;
	tsm_vte_charset *g1;
	tsm_vte_charset *g2;
	tsm_vte_charset *g3;

	struct tsm_vte_saved_state saved_state;
	unsigned int alt_cursor_x;
	unsigned int alt_cursor_y;
};

/* utf8 state machine */

enum tsm_utf8_mach_state {
	TSM_UTF8_START,
	TSM_UTF8_ACCEPT,
	TSM_UTF8_REJECT,
	TSM_UTF8_EXPECT1,
	TSM_UTF8_EXPECT2,
	TSM_UTF8_EXPECT3,
};

int tsm_utf8_mach_new(struct tsm_utf8_mach *out);

int tsm_utf8_mach_feed(struct tsm_utf8_mach *mach, char c);
uint32_t tsm_utf8_mach_get(struct tsm_utf8_mach *mach);
void tsm_utf8_mach_reset(struct tsm_utf8_mach *mach);

#define SELECTION_TOP -1

void screen_cell_init(struct tsm_screen *con, struct tsm_cell *cell);

void tsm_screen_set_opts(struct tsm_screen *scr, unsigned int opts);
void tsm_screen_reset_opts(struct tsm_screen *scr, unsigned int opts);
unsigned int tsm_screen_get_opts(struct tsm_screen *scr);

static inline void screen_inc_age(struct tsm_screen *con)
{
	if (!++con->age_cnt) {
		con->age_reset = 1;
		++con->age_cnt;
	}
}

typedef int (*tsm_screen_draw_cb)(struct tsm_screen *con, uint64_t id, const uint32_t *ch, size_t len,
								  unsigned int width, unsigned int posx, unsigned int posy,
								  const struct tsm_screen_attr *attr, tsm_age_t age, void *data);

int tsm_screen_new(struct tsm_screen *out);
void tsm_screen_free(struct tsm_screen *con);

unsigned int tsm_screen_get_width(struct tsm_screen *con);
unsigned int tsm_screen_get_height(struct tsm_screen *con);
int tsm_screen_resize(struct tsm_screen *con, unsigned int x, unsigned int y);
int tsm_screen_set_margins(struct tsm_screen *con, unsigned int top, unsigned int bottom);

void tsm_screen_set_def_attr(struct tsm_screen *con, const struct tsm_screen_attr *attr);
void tsm_screen_reset(struct tsm_screen *con);
void tsm_screen_set_flags(struct tsm_screen *con, unsigned int flags);
void tsm_screen_reset_flags(struct tsm_screen *con, unsigned int flags);
unsigned int tsm_screen_get_flags(struct tsm_screen *con);

unsigned int tsm_screen_get_cursor_x(struct tsm_screen *con);
unsigned int tsm_screen_get_cursor_y(struct tsm_screen *con);

void tsm_screen_set_tabstop(struct tsm_screen *con);
void tsm_screen_reset_tabstop(struct tsm_screen *con);
void tsm_screen_reset_all_tabstops(struct tsm_screen *con);

void tsm_screen_write(struct tsm_screen *con, tsm_symbol_t ch, const struct tsm_screen_attr *attr);
void tsm_screen_newline(struct tsm_screen *con);
void tsm_screen_move_to(struct tsm_screen *con, unsigned int x, unsigned int y);
void tsm_screen_move_up(struct tsm_screen *con, unsigned int num, bool scroll);
void tsm_screen_move_down(struct tsm_screen *con, unsigned int num, bool scroll);
void tsm_screen_move_left(struct tsm_screen *con, unsigned int num);
void tsm_screen_move_right(struct tsm_screen *con, unsigned int num);
void tsm_screen_move_line_end(struct tsm_screen *con);
void tsm_screen_move_line_home(struct tsm_screen *con);
void tsm_screen_tab_right(struct tsm_screen *con, unsigned int num);
void tsm_screen_tab_left(struct tsm_screen *con, unsigned int num);
void tsm_screen_insert_lines(struct tsm_screen *con, unsigned int num);
void tsm_screen_delete_lines(struct tsm_screen *con, unsigned int num);
void tsm_screen_insert_chars(struct tsm_screen *con, unsigned int num);
void tsm_screen_delete_chars(struct tsm_screen *con, unsigned int num);
void tsm_screen_erase_cursor(struct tsm_screen *con);
void tsm_screen_erase_chars(struct tsm_screen *con, unsigned int num);
void tsm_screen_erase_cursor_to_end(struct tsm_screen *con, bool protect);
void tsm_screen_erase_home_to_cursor(struct tsm_screen *con, bool protect);
void tsm_screen_erase_current_line(struct tsm_screen *con, bool protect);
void tsm_screen_erase_screen_to_cursor(struct tsm_screen *con, bool protect);
void tsm_screen_erase_cursor_to_screen(struct tsm_screen *con, bool protect);
void tsm_screen_erase_screen(struct tsm_screen *con, bool protect);
void tsm_screen_scroll_up(struct tsm_screen *con, unsigned int num);
void tsm_screen_scroll_down(struct tsm_screen *con, unsigned int num);
void tsm_screen_selection_reset(struct tsm_screen *con);
void tsm_screen_selection_start(struct tsm_screen *con, unsigned int posx, unsigned int posy);
void tsm_screen_selection_target(struct tsm_screen *con, unsigned int posx, unsigned int posy);
void tsm_screen_selection_word(struct tsm_screen *con, unsigned int posx, unsigned int posy);
int tsm_screen_selection_copy(struct tsm_screen *con, char *out, int out_size);

tsm_age_t tsm_screen_draw(struct tsm_screen *con, tsm_screen_draw_cb draw_cb, void *data);

/** @} */

/**
 * @defgroup vte State Machine
 * Virtual terminal emulation with state machine
 *
 * A TSM VTE object provides the terminal state machine. It takes input from the
 * application (which usually comes from a TTY/PTY from a client), parses it,
 * modifies the attach screen or returns data which has to be written back to
 * the client.
 *
 * Furthermore, VTE objects accept keyboard or mouse input from the application
 * which is interpreted compliant to DEV-VTs.
 *
 * @{
 */

/* virtual terminal emulator */

struct tsm_vte;

/**
 * @brief Set color palette to one of the predefined palette on the vte object.
 *
 * Current supported palette names are
 *
 * - solarized
 * - solarized-black
 * - solarized-white
 * - soft-black
 * - base16-dark
 * - base16-light
 *
 * In addition, when palette name is "custom", the custom palette set in
 * tsm_vte_set_custom_palette() is used.
 *
 * @sa tsm_vte_set_custom_palette to set custom palette.
 *
 * @param vte The vte object to set on.
 * @param palette_name Name of the color palette. Pass NULL to reset to default.
 *
 * @retval 0 on success.
 * @retval -EINVAL if vte is NULL.
 * @retval -ENOMEM if malloc fails.
 */
int tsm_vte_set_palette(struct tsm_vte *vte, enum tsm_palette palette_name);

void tsm_vte_get_def_attr(struct tsm_vte *vte, struct tsm_screen_attr *out);
unsigned int tsm_vte_get_flags(struct tsm_vte *vte);

unsigned int tsm_vte_get_mouse_mode(struct tsm_vte *vte);
unsigned int tsm_vte_get_mouse_event(struct tsm_vte *vte);

void tsm_vte_reset(struct tsm_vte *vte);
void tsm_vte_hard_reset(struct tsm_vte *vte);
void tsm_vte_input(struct tsm_vte *vte, const char *u8, size_t len);

/**
 * @brief Set backspace key to send either backspace or delete.
 *
 * Some terminals send ASCII backspace (010, 8, 0x08), some send ASCII delete
 * (0177, 127, 0x7f).
 *
 * The default for vte is to send ASCII backspace.
 *
 * @param vte The vte object to set on
 * @param enable Send ASCII delete if \c true, send ASCII backspace if \c false.
 */
void tsm_vte_set_backspace_sends_delete(struct tsm_vte *vte, bool enable);
bool tsm_vte_handle_keyboard(struct tsm_vte *vte, uint32_t keysym, uint32_t ascii, unsigned int mods,
							 uint32_t unicode);
bool tsm_vte_handle_mouse(struct tsm_vte *vte, unsigned int cell_x, unsigned int cell_y, unsigned int pixel_x,
						  unsigned int pixel_y, unsigned int button, unsigned int event, unsigned char flags);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* TSM_LIBTSM_H */
