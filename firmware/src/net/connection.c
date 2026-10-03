#include "connection.h"

#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(termlet_connection);

static void close_fd(int *fd) {
	if (*fd < 0)
		return;

	(void) zsock_shutdown(*fd, ZSOCK_SHUT_RDWR);
	(void) zsock_close(*fd);

	*fd = -1;
}

static int create_listener(struct termlet_connection *connection) {
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(connection->port),
		.sin_addr = {
		    .s_addr = htonl(INADDR_ANY),
		},
	};

	int reuse = 1;
	int fd;
	int ret;

	fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0)
		return -errno;

	ret = zsock_setsockopt(fd,
	                       SOL_SOCKET,
	                       SO_REUSEADDR,
	                       &reuse,
	                       sizeof(reuse));
	if (ret < 0)
		goto error;

	ret = zsock_bind(fd,
	                 (struct sockaddr *) &address,
	                 sizeof(address));
	if (ret < 0)
		goto error;

	ret = zsock_listen(fd, 1);
	if (ret < 0)
		goto error;

	connection->listen_fd = fd;

	LOG_INF("TCP server listening on port %u", connection->port);

	return 0;

error:
	ret = -errno;
	(void) zsock_close(fd);
	return ret;
}

int termlet_connection_init(struct termlet_connection *connection,
                            uint16_t port) {
	if (connection == NULL)
		return -EINVAL;

	connection->port = port;
	connection->listen_fd = -1;
	connection->client_fd = -1;

	k_mutex_init(&connection->write_lock);

	return 0;
}

int termlet_connection_accept(struct termlet_connection *connection) {
	int fd;
	int ret;

	if (connection == NULL)
		return -EINVAL;

	if (connection->client_fd >= 0)
		return -EALREADY;

	if (connection->listen_fd < 0) {
		ret = create_listener(connection);
		if (ret < 0)
			return ret;
	}

	for (;;) {
		fd = zsock_accept(connection->listen_fd, NULL, NULL);

		if (fd >= 0)
			break;

		if (errno == EINTR)
			continue;

		/*
		 * If accept itself fails, discard the listener as well.
		 * The next attempt can create a clean socket.
		 */
		ret = -errno;
		close_fd(&connection->listen_fd);

		return ret;
	}

	connection->client_fd = fd;

	return 0;
}

ssize_t termlet_connection_read(struct termlet_connection *connection,
                                void *buffer,
                                size_t size) {
	ssize_t ret;

	if (connection == NULL ||
	    (buffer == NULL && size != 0))
		return -EINVAL;

	if (connection->client_fd < 0)
		return -ENOTCONN;

	for (;;) {
		ret = zsock_recv(connection->client_fd,
		                 buffer,
		                 size,
		                 0);

		if (ret >= 0)
			return ret;

		if (errno != EINTR)
			return -errno;
	}
}

int termlet_connection_write(struct termlet_connection *connection,
                             const void *buffer,
                             size_t size) {
	const uint8_t *data = buffer;
	size_t offset = 0;
	int ret = 0;

	if (connection == NULL ||
	    (buffer == NULL && size != 0))
		return -EINVAL;

	k_mutex_lock(&connection->write_lock, K_FOREVER);

	if (connection->client_fd < 0) {
		ret = -ENOTCONN;
		goto out;
	}

	while (offset < size) {
		ssize_t written =
		    zsock_send(connection->client_fd,
		               data + offset,
		               size - offset,
		               0);

		if (written > 0) {
			offset += (size_t) written;
			continue;
		}

		if (written < 0 && errno == EINTR)
			continue;

		ret = written == 0 ? -ECONNRESET : -errno;
		break;
	}

out:
	k_mutex_unlock(&connection->write_lock);

	return ret;
}

void termlet_connection_close_client(struct termlet_connection *connection) {
	if (connection == NULL)
		return;

	/*
	 * Synchronize with termlet_connection_write(), so the descriptor
	 * cannot be closed underneath an active writer.
	 */
	k_mutex_lock(&connection->write_lock, K_FOREVER);
	close_fd(&connection->client_fd);
	k_mutex_unlock(&connection->write_lock);
}

void termlet_connection_deinit(struct termlet_connection *connection) {
	if (connection == NULL)
		return;

	termlet_connection_close_client(connection);
	close_fd(&connection->listen_fd);
}
