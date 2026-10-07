#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <zephyr/kernel.h>

struct termlet_server;
struct termlet_conn;

typedef void (*termlet_conn_accept_cb)(struct termlet_server *, struct termlet_conn *, void *);
typedef void (*termlet_conn_poll_cb)(struct termlet_conn *, int, void *);

struct termlet_conn {
	struct termlet_server *srv;
	int fd;
	termlet_conn_poll_cb poll;
	void *userdata;

	/*
	 * Multiple producers may write to the Telnet stream, e.g. keyboard
	 * input and terminal responses. Keep individual writes serialized.
	 */
	struct k_mutex write_lock;
};

struct termlet_server {
	uint16_t port;
	int listen_fd;

	termlet_conn_accept_cb accept;
	void *userdata;

	struct termlet_conn conns[CONFIG_ZVFS_POLL_MAX - 1];
};

int termlet_server_create(struct termlet_server *server, uint16_t port, termlet_conn_accept_cb accept);

int termlet_server_poll(struct termlet_server *server, k_timeout_t timeout);

ssize_t termlet_conn_read(struct termlet_conn *conn, void *buffer, size_t size);

int termlet_conn_write(struct termlet_conn *conn, const void *buffer, size_t size);

void termlet_conn_close(struct termlet_conn *conn);

void termlet_server_close(struct termlet_server *server);
