#include "connection.h"

#include "zephyr/net/socket_poll.h"
#include "zephyr/sys/clock.h"
#include "zephyr/sys/util.h"

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

static int create_listener(struct termlet_server *server) {
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(server->port),
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

	server->listen_fd = fd;

	LOG_INF("TCP server listening on port %u", server->port);

	return 0;

error:
	ret = -errno;
	(void) zsock_close(fd);
	return ret;
}

int termlet_server_create(struct termlet_server *server, uint16_t port, termlet_conn_accept_cb accept) {
	if (server == NULL)
		return -EINVAL;

	memset(server, 0, sizeof(*server));

	server->port = port;
	server->accept = accept;

	server->listen_fd = -1;

	for (int i = 0; i < (int) ARRAY_SIZE(server->conns); i++) {
		server->conns[i].fd = -1;
		k_mutex_init(&server->conns[i].write_lock);
	}

	return 0;
}

int termlet_server_poll(struct termlet_server *server, k_timeout_t timeout) {
	if (server == NULL)
		return -EINVAL;

	if (server->listen_fd < 0) {
		int ret = create_listener(server);
		if (ret < 0)
			return ret;
	}

	struct zsock_pollfd pollfds[ARRAY_SIZE(server->conns) + 1];
	struct termlet_conn *pollconns[ARRAY_SIZE(server->conns) + 1];

	int npollfd = 0;
	int free_slot = -1;

	for (int i = 0; i < (int) ARRAY_SIZE(server->conns); i++) {
		struct termlet_conn *conn = &server->conns[i];

		if (conn->fd < 0) {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}

		pollfds[npollfd] = (struct zsock_pollfd){
			.fd = conn->fd,
			.events = ZSOCK_POLLIN,
		};

		pollconns[npollfd] = conn;
		npollfd++;
	}

	/*
	 * Only poll the listener when there is somewhere to store a
	 * newly accepted connection.
	 */
	if (free_slot >= 0) {
		pollfds[npollfd] = (struct zsock_pollfd){
			.fd = server->listen_fd,
			.events = ZSOCK_POLLIN,
		};

		pollconns[npollfd] = NULL;
		npollfd++;
	}

	int ms;

	if (K_TIMEOUT_EQ(timeout, K_FOREVER))
		ms = -1;
	else if (K_TIMEOUT_EQ(timeout, K_NO_WAIT))
		ms = 0;
	else
		ms = (int) k_ticks_to_ms_ceil32(timeout.ticks);

	int ready = zsock_poll(pollfds, npollfd, ms);

	if (ready < 0) {
		if (errno == EINTR)
			return 0;

		return -errno;
	}

	if (ready == 0)
		return 0;

	for (int i = 0; i < npollfd && ready > 0; i++) {
		if (pollfds[i].revents == 0)
			continue;

		ready--;

		struct termlet_conn *conn = pollconns[i];

		if (conn == NULL) {
			int fd = zsock_accept(server->listen_fd, NULL, NULL);
			if (fd < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					continue;

				return -errno;
			}

			conn = &server->conns[free_slot];
			conn->fd = fd;

			if (server->accept)
				server->accept(server, conn, server->userdata);

			continue;
		}

		if (conn->poll)
			conn->poll(conn, pollfds[i].revents,
			           conn->userdata);
	}

	return 0;
}
ssize_t termlet_conn_read(struct termlet_conn *conn, void *buffer, size_t size) {
	ssize_t ret;

	if (conn->fd == -1 || conn == NULL ||
	    (buffer == NULL && size != 0))
		return -EINVAL;

	for (;;) {
		ret = zsock_recv(conn->fd, buffer, size, 0);

		if (ret >= 0)
			return ret;

		if (errno != EINTR)
			return -errno;
	}
}

int termlet_conn_write(struct termlet_conn *conn, const void *buffer, size_t size) {
	const uint8_t *data = buffer;
	size_t offset = 0;
	int ret = 0;

	if (conn->fd == -1 || conn == NULL || (buffer == NULL && size != 0))
		return -EINVAL;

	k_mutex_lock(&conn->write_lock, K_FOREVER);

	if (conn->fd < 0) {
		ret = -ENOTCONN;
		goto out;
	}

	while (offset < size) {
		ssize_t written =
		    zsock_send(conn->fd, data + offset, size - offset, 0);

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
	k_mutex_unlock(&conn->write_lock);

	return ret;
}

void termlet_conn_close(struct termlet_conn *conn) {
	if (conn->fd == -1 || conn == NULL)
		return;

	/*
	 * Synchronize with termlet_connection_write(), so the descriptor
	 * cannot be closed underneath an active writer.
	 */
	k_mutex_lock(&conn->write_lock, K_FOREVER);
	close_fd(&conn->fd);
	k_mutex_unlock(&conn->write_lock);
}

void termlet_server_close(struct termlet_server *srv) {
	if (srv == NULL)
		return;

	for (int i = 0; i < (int) ARRAY_SIZE(srv->conns); i++)
		termlet_conn_close(&srv->conns[i]);

	close_fd(&srv->listen_fd);
}
