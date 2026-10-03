#include "net/connection.h"
#include "net/network.h"
#include "window/chars.h"
#include "window/layout.h"
#include "zephyr/sys/util.h"

#include <errno.h>
#include <fonts.h>
#include <keymap.h>
#include <libtsm.h>
#include <libutf8.h>
#include <minitelnet-def.h>
#include <minitelnet-negotiation.h>
#include <minitelnet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app);

#define DISPLAY_WIDTH  1024
#define DISPLAY_HEIGHT 600
#define TERMLET_PORT   2323


static struct telnet telnet;
static struct tsm_screen screen;
static struct tsm_vte vte;
static struct termlet_connection connection;
static struct termlet_network network;

static struct win_layout layout = { 0 };
static int terminal_window = 0;

static uint16_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];

struct renderer {
	const struct device *display;
	const struct font *font;

	uint16_t *buffer;
	uint16_t width;
	uint16_t height;
	uint16_t pitch;

	unsigned int columns;
	unsigned int rows;
};

static inline uint16_t rgb888_to_rgb565(uint8_t r,
                                        uint8_t g,
                                        uint8_t b) {
	return ((uint16_t) (r >> 3) << 11) |
	       ((uint16_t) (g >> 2) << 5) |
	       ((uint16_t) (b >> 3));
}

static inline uint16_t blend_rgb565(uint16_t bg,
                                    uint16_t fg,
                                    uint8_t coverage) {
	uint32_t br = (bg >> 11) & 0x1f;
	uint32_t bgc = (bg >> 5) & 0x3f;
	uint32_t bb = bg & 0x1f;

	uint32_t fr = (fg >> 11) & 0x1f;
	uint32_t fgc = (fg >> 5) & 0x3f;
	uint32_t fb = fg & 0x1f;

	uint32_t r = (br * (15 - coverage) + fr * coverage) / 15;
	uint32_t g = (bgc * (15 - coverage) + fgc * coverage) / 15;
	uint32_t b = (bb * (15 - coverage) + fb * coverage) / 15;

	return (uint16_t) ((r << 11) | (g << 5) | b);
}

static void draw_glyph(struct renderer *renderer,
                       uint32_t codepoint,
                       unsigned int x,
                       unsigned int y,
                       uint16_t fg,
                       uint16_t bg) {
	const struct font *font = renderer->font;
	const uint8_t *glyph = font_get_glyph(font, codepoint);
	uint16_t colors[16];

	for (unsigned int i = 0; i < ARRAY_SIZE(colors); i++)
		colors[i] = blend_rgb565(bg, fg, i);

	for (unsigned int gy = 0; gy < font->height; gy++) {
		unsigned int py = y + gy;

		if (py >= renderer->height)
			break;

		for (unsigned int gx = 0; gx < font->width; gx++) {
			unsigned int px = x + gx;

			if (px >= renderer->width)
				break;

			uint8_t coverage = font_glyph_coverage(font, glyph, gx, gy);
			renderer->buffer[py * renderer->pitch + px] = colors[coverage];
		}
	}
}

static int draw_cb(struct tsm_screen *screen,
                   uint64_t id,
                   const uint32_t *ch,
                   size_t len,
                   unsigned int width,
                   unsigned int posx,
                   unsigned int posy,
                   const struct tsm_screen_attr *attr,
                   tsm_age_t age,
                   void *data) {

	struct win_char c;
	c.code = ch[0];
	c.fg = rgb888_to_rgb565(attr->fr, attr->fg, attr->fb);
	c.bg = rgb888_to_rgb565(attr->br, attr->bg, attr->bb);

	if (attr->inverse) {
		uint16_t tmp = c.fg;
		c.fg = c.bg;
		c.bg = tmp;
	}

	win_layout_put(&layout, terminal_window, posx, posy, c);
	return 0;
}

/* libtsm-responses en keyboard-output gaan terug naar de TCP-client. */
static void vte_write_cb(struct tsm_vte *vte,
                         const char *u8,
                         size_t len,
                         void *data) {
	struct termlet_connection *connection = data;
	int ret;

	ARG_UNUSED(vte);

	ret = termlet_connection_write(connection, u8, len);
	if (ret < 0 && ret != -ENOTCONN)
		LOG_WRN("TCP write failed: %d", ret);
}

keymap_state_t keymap_state;
keymap_compose_state_t compose_state;
char compose_text[16];

static bool vte_handle_keyboard(uint32_t keysym,
                                const char *utf8,
                                keymap_state_t state) {
	const char *p;
	int plen;
	uint32_t cp;
	size_t n;
	unsigned int mods = 0;

	if (state & KEYMAP_SHIFT_MASK)
		mods |= TSM_SHIFT_MASK;
	if (state & KEYMAP_NUM)
		mods |= TSM_LOCK_MASK;
	if (state & KEYMAP_CTRL_MASK)
		mods |= TSM_CONTROL_MASK;
	if (state & KEYMAP_ALT_MASK)
		mods |= TSM_ALT_MASK;
	if (state & KEYMAP_SUPER_MASK)
		mods |= TSM_LOGO_MASK;

	if (!*utf8) {
		return tsm_vte_handle_keyboard(&vte,
		                               keysym,
		                               XKB_KEY_NoSymbol,
		                               mods,
		                               TSM_VTE_INVALID);
	}

	p = utf8;
	plen = strlen(utf8);
	n = utf8_decode(p, plen, &cp);
	if (!n)
		return false;

	if (p[n] == '\0') {
		return tsm_vte_handle_keyboard(&vte,
		                               keysym,
		                               XKB_KEY_NoSymbol,
		                               mods,
		                               cp);
	}

	do {
		if (!tsm_vte_handle_keyboard(&vte,
		                             XKB_KEY_NoSymbol,
		                             XKB_KEY_NoSymbol,
		                             0,
		                             cp))
			return false;

		p += n;
		plen -= n;
		n = utf8_decode(p, plen, &cp);
	} while (n);

	return true;
}

void handle_keyboard(struct input_event *evt, void *user_data) {
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY)
		return;

	const char *utf8 = "";
	uint32_t sym = keymap_lookup(&keymap_state, evt->code, evt->value, &utf8);

	if (evt->value == 0)
		return;

	switch (keymap_compose_feed(&compose_state, &sym, &utf8)) {
		case KEYMAP_COMPOSE_COMPOSING:
			strcpy(compose_text + strlen(compose_text), utf8);
			break;

		case KEYMAP_COMPOSE_CANCELLED:
			compose_text[0] = '\0';
			break;

		case KEYMAP_COMPOSE_COMPOSED:
		case KEYMAP_COMPOSE_NOTHING:
			compose_text[0] = '\0';
			LOG_INF("key %d %s", sym, utf8 ? utf8 : "");
			vte_handle_keyboard(sym, utf8, keymap_state);
			break;
	}
}

struct renderer renderer = {
	.display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display)),
	.buffer = framebuffer,
};

struct termlet_state {
	struct telnet *telnet;
	telnet_negotiation_t neg;
};
static enum telnet_command option_policy(enum telnet_command request,
                                         unsigned char option) {
	if (request == TELNET_CMD_DO) {
		switch (option) {
			case TELNET_OPT_TTYPE:
			case TELNET_OPT_NAWS:
			case TELNET_OPT_BINARY:
				return TELNET_CMD_WILL;

			default:
				return TELNET_CMD_WONT;
		}
	}

	if (request == TELNET_CMD_WILL) {
		switch (option) {
			case TELNET_OPT_BINARY:
				return TELNET_CMD_DO;

			default:
				return TELNET_CMD_DONT;
		}
	}

	return 0;
}
static void send_naws(struct termlet_state *state) {
	unsigned char out[4];

	out[0] = (renderer.columns >> 8) & 0xff;
	out[1] = renderer.columns & 0xff;
	out[2] = (renderer.rows >> 8) & 0xff;
	out[3] = renderer.rows & 0xff;

	telnet_send_subnegotiation(state->telnet,
	                           TELNET_OPT_NAWS,
	                           out,
	                           sizeof out);

	telnet_send_subnegotiation_end(state->telnet,
	                               TELNET_OPT_NAWS);
}

static void handle_transition(
    struct termlet_state *state,
    const struct telnet_negotiation_transition *trns) {
	if (trns->outgoing) {
		telnet_send_negotiation(
		    state->telnet,
		    trns->outgoing,
		    trns->option);
	}

	if (trns->option == TELNET_OPT_NAWS &&
	    trns->local &&
	    trns->state == TELNET_OPTION_YES) {
		send_naws(state);
	}
}
static void handle_negotiation(struct termlet_state *state,
                               enum telnet_command cmd,
                               unsigned char option) {
	struct telnet_negotiation_transition trns;
	enum telnet_command response;

	if (!telnet_negotiation_feed(state->neg, cmd, option, &trns))
		return;

	if (trns.state == TELNET_OPTION_REQUEST_PENDING) {
		response = option_policy(cmd, option);

		if (response != 0 &&
		    telnet_negotiation_respond(
		        state->neg,
		        response,
		        option,
		        &trns)) {
			handle_transition(state, &trns);
		}

		return;
	}

	handle_transition(state, &trns);
}
void handle_subnegotiation(struct telnet *telnet, const union telnet_event *s) {
	static char buffer[20];
	static size_t buffer_size = 0;

	char out[20];

	if (s->data.buffer != NULL && s->data.size != 0) {
		size_t p = MIN(s->data.size, sizeof(buffer) - buffer_size);
		if (p > 0) {
			memcpy(buffer + buffer_size, s->data.buffer, p);
			buffer_size += p;
		}
		return;
	}

	switch (s->subneg.option) {
		case TELNET_OPT_TTYPE:
			if (buffer[0] != TELNET_TTYPE_SEND)
				return;

			static const char term[] = "xterm-256color";

			out[0] = TELNET_TTYPE_IS;
			memcpy(out + 1, term, sizeof(term) - 1);

			telnet_send_subnegotiation(
			    telnet,
			    TELNET_OPT_TTYPE,
			    out,
			    1 + sizeof(term) - 1);

			telnet_send_subnegotiation_end(
			    telnet,
			    TELNET_OPT_TTYPE);
			break;
	}
	buffer_size = 0;
	return;
}

void handle_telnet(struct telnet *telnet, const union telnet_event *ev, void *userdata) {
	struct termlet_state *state = userdata;
	int ret;

	switch (ev->type) {
		case TELNET_EV_DATA:
			tsm_vte_input(&vte, ev->data.buffer, ev->data.size);

			tsm_screen_draw(&screen, draw_cb, NULL);
			win_layout_render(&layout);
			break;

		case TELNET_EV_SEND:
			ret = termlet_connection_write(&connection, ev->data.buffer, ev->data.size);
			if (ret < 0 && ret != -ENOTCONN)
				LOG_WRN("TCP write failed: %d", ret);
			break;

		case TELNET_EV_SUBNEG:
			handle_subnegotiation(telnet, ev);
			break;

		case TELNET_EV_NEG:
			handle_negotiation(state, ev->command.code, ev->neg.option);
			break;

		case TELNET_EV_ERROR:
			LOG_ERR("error from telnet: %d", ev->error.code);
			break;

		case TELNET_EV_COMMAND:
			LOG_WRN("unhandled command from telnet: %d", ev->command.code);
			break;
	}
}

void draw_char(uint16_t posx, uint16_t posy,
               struct win_char ch, void *userdata) {
	struct renderer *renderer = userdata;
	const struct font *font = renderer->font;

	unsigned int x = posx * font->width;
	unsigned int y = posy * font->height;

	if (x >= renderer->width || y >= renderer->height)
		return;

	draw_glyph(renderer, ch.code, x, y, ch.fg, ch.bg);
}

static void commit(void *userdata) {
	struct renderer *r = userdata;

	struct display_buffer_descriptor desc = {
		.buf_size = r->pitch * r->height * sizeof(uint16_t),
		.width = r->width,
		.height = r->height,
		.pitch = r->pitch,
		.frame_incomplete = false,
	};

	int ret = display_write(r->display, 0, 0, &desc, r->buffer);
	if (ret < 0)
		LOG_ERR("display_write failed: %d", ret);
}

static void resize_handler(struct win_layout *layout, int win,
                           uint16_t cols, uint16_t rows,
                           bool visible, void *userdata) {
	ARG_UNUSED(layout);
	ARG_UNUSED(win);

	struct termlet_state *state = userdata;
	;


	if (!visible)
		return;

	if (tsm_screen_get_width(&screen) == cols &&
	    tsm_screen_get_height(&screen) == rows)
		return;

	int ret = tsm_screen_resize(&screen, cols, rows);
	if (ret < 0) {
		LOG_ERR("tsm_screen_resize failed: %d", ret);
		return;
	}

	renderer.columns = cols;
	renderer.rows = rows;

	send_naws(state);

	LOG_INF("resized to %dx%d", cols, rows);
}

static void status_resize_handler(struct win_layout *layout, int win,
                                  uint16_t cols, uint16_t rows,
                                  bool visible, void *userdata) {
	LOG_INF("status: %d %d %d", visible, cols, rows);
}

int main(void) {
	int ret;

	if (!device_is_ready(renderer.display)) {
		LOG_INF("Display not ready");
		return 0;
	}

	ret = display_set_pixel_format(renderer.display, PIXEL_FORMAT_RGB_565);
	if (ret < 0) {
		LOG_INF("Unable to select RGB565: %d", ret);
		return 0;
	}

	struct display_capabilities cap;
	display_get_capabilities(renderer.display, &cap);
	LOG_INF("Display: %ux%u", cap.x_resolution, cap.y_resolution);

	if (cap.x_resolution != DISPLAY_WIDTH ||
	    cap.y_resolution != DISPLAY_HEIGHT) {
		LOG_INF("Unexpected display resolution: %ux%u, expected %ux%u",
		        cap.x_resolution,
		        cap.y_resolution,
		        DISPLAY_WIDTH,
		        DISPLAY_HEIGHT);
		return 0;
	}

	renderer.width = cap.x_resolution;
	renderer.pitch = cap.x_resolution;
	renderer.height = cap.y_resolution;

	ret = display_blanking_off(renderer.display);
	if (ret < 0)
		LOG_INF("display_blanking_off failed: %d", ret);

	renderer.font = fonts[0];
	LOG_INF("Font: %s %s %u, cell=%ux%u",
	        renderer.font->name,
	        renderer.font->style,
	        renderer.font->size,
	        renderer.font->width,
	        renderer.font->height);

	renderer.columns = cap.x_resolution / renderer.font->width;
	renderer.rows = cap.y_resolution / renderer.font->height;

	layout.bounds = (struct win_rect){
		.x0 = 0,
		.y0 = 0,
		.x1 = renderer.columns,
		.y1 = renderer.rows,
	};
	layout.draw_char = draw_char;
	layout.commit = commit;
	layout.userdata = &renderer;

	LOG_INF("Terminal: %ux%u", renderer.columns, renderer.rows);

	struct termlet_state state = { 0 };

	ret = tsm_screen_new(&screen);
	if (ret < 0) {
		LOG_INF("tsm_screen_new failed: %d", ret);
		return 0;
	}

	/*
	 * De TCP-laag wordt eerst geïnitialiseerd zodat network.c vanaf het
	 * eerste connectivity-event veilig ready/down kan doorgeven.
	 */
	ret = termlet_connection_init(&connection, TERMLET_PORT);
	if (ret < 0) {
		LOG_ERR("TCP connection setup failed: %d", ret);
		return 0;
	}

	ret = termlet_network_init(&network);
	if (ret < 0) {
		LOG_ERR("Network setup failed: %d", ret);
		return 0;
	}

	state.telnet = &telnet;
	telnet_init(&telnet, handle_telnet, &state);

	int status_window = win_layout_new_clip(&layout, status_resize_handler, NULL, 0, WIN_EDGE_BOTTOM, 1);
	win_layout_enable(&layout, status_window, true);

	struct win_char ch = {
		.code = '@',
		.fg = 0x1234,
		.bg = 0xfedc,
	};
	win_layout_put(&layout, status_window, 0, 0, ch);

	terminal_window = win_layout_new_main(&layout, resize_handler, &state, 0);
	win_layout_enable(&layout, terminal_window, true);

	ret = tsm_vte_new(&vte, &screen, vte_write_cb, &connection);
	if (ret < 0) {
		LOG_INF("tsm_vte_new failed: %d", ret);
		termlet_connection_deinit(&connection);
		return 0;
	}

	ret = tsm_vte_set_palette(&vte, TSM_PALETTE_BASE16_DARK);
	if (ret < 0) {
		LOG_INF("tsm_vte_set_palette failed: %d", ret);
		termlet_connection_deinit(&connection);
		return 0;
	}

	memset(framebuffer, 0, sizeof(framebuffer));

	tsm_screen_draw(&screen, draw_cb, NULL);
	win_layout_render(&layout);

	LOG_INF("Termlet initialized; waiting for IPv4 connectivity");

	static uint8_t buffer[8192];

	for (;;) {
		/*
		 * This normally returns immediately. It only blocks when DHCP/IP
		 * connectivity has actually disappeared.
		 */
		termlet_network_wait_ready(&network);

		LOG_INF("Waiting for terminal client");

		ret = termlet_connection_accept(&connection);
		if (ret < 0) {
			LOG_WRN("TCP accept failed: %d", ret);
			termlet_connection_close_client(&connection);
			k_sleep(K_MSEC(250));
			continue;
		}

		LOG_INF("Terminal client connected");

		for (;;) {
			ssize_t len = termlet_connection_read(
			    &connection,
			    buffer,
			    sizeof(buffer));

			if (len == 0) {
				LOG_INF("Terminal client disconnected");
				break;
			}

			if (len < 0) {
				LOG_WRN("TCP read failed: %d", (int) len);
				break;
			}

			telnet_feed(&telnet, buffer, len);
		}

		termlet_connection_close_client(&connection);

		telnet_reset(&telnet);
		memset(state.neg, 0, sizeof(state.neg));
	}
	return 0;
}

INPUT_CALLBACK_DEFINE(NULL, handle_keyboard, NULL);
