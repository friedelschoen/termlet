#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <zephyr/kernel.h>

struct termlet_connection {
	uint16_t port;

	int listen_fd;
	int client_fd;

	/*
	 * Multiple producers may write to the Telnet stream, e.g. keyboard
	 * input and terminal responses. Keep individual writes serialized.
	 */
	struct k_mutex write_lock;
};

int termlet_connection_init(struct termlet_connection *connection,
                            uint16_t port);

int termlet_connection_accept(struct termlet_connection *connection);

ssize_t termlet_connection_read(struct termlet_connection *connection,
                                void *buffer,
                                size_t size);

int termlet_connection_write(struct termlet_connection *connection,
                             const void *buffer,
                             size_t size);

void termlet_connection_close_client(struct termlet_connection *connection);

void termlet_connection_deinit(struct termlet_connection *connection);
