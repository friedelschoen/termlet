/** termlet-run -- run a program on a termlet-instance
 *
 * EXITCODES
 *  1 - usage
 *  2 - connectivity
 *
 *  ... exitcodes from child
 */

#include "arg.h"

#include <arpa/inet.h>
#include <errno.h>
#include <minitelnet-def.h>
#include <minitelnet-negotiation.h>
#include <minitelnet.h>
#include <netdb.h>
#include <poll.h>
#include <pty.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PROGRAM        "termlet-run"
#define DEFAULT_WIDTH  80
#define DEFAULT_HEIGHT 24
#define TERMNAME_MAX   64

enum session_state {
	SESSION_NEGOTIATING,
	SESSION_RUNNING,
	SESSION_DONE,
};

struct termlet_state {
	enum session_state state;

	struct telnet telnet;
	telnet_negotiation_t negotiation;

	int tcp_socket;
	int pty_master;
	pid_t child_pid;

	char **child_argv;

	char termname[TERMNAME_MAX];
	unsigned char ttype_command;
	int ttype_have_command;
	size_t ttype_size;
	int ttype_overflow;

	unsigned char naws[4];
	size_t naws_size;
	int naws_overflow;

	uint16_t width;
	uint16_t height;

	int have_ttype;
	int have_naws;
};

static void usage(int exitcode) {
	fprintf(stderr,
	        "usage: " PROGRAM " [-p port] remote command [args ...]\n");
	exit(exitcode);
}

static int create_socket(const char *hostname, const char *port) {
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *rp;
	int rc;
	int sock = -1;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo(hostname, port, &hints, &result);
	if (rc != 0) {
		fprintf(stderr,
		        PROGRAM ": unable to resolve address '%s:%s': %s\n",
		        hostname, port, gai_strerror(rc));
		return -1;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sock = socket(rp->ai_family,
		              rp->ai_socktype,
		              rp->ai_protocol);

		if (sock < 0)
			continue;

		if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		close(sock);
		sock = -1;
	}

	freeaddrinfo(result);

	if (sock < 0) {
		fprintf(stderr,
		        PROGRAM ": unable to connect to '%s:%s'\n",
		        hostname, port);
	}

	return sock;
}

static int write_all(int fd, const void *buffer, size_t size) {
	const unsigned char *p = buffer;

	while (size > 0) {
		ssize_t n = write(fd, p, size);

		if (n > 0) {
			p += n;
			size -= (size_t) n;
			continue;
		}

		if (n < 0 && errno == EINTR)
			continue;

		return -1;
	}

	return 0;
}

static int read_telnet(struct telnet *telnet, int fd) {
	unsigned char buffer[4096];
	ssize_t n;

	do {
		n = read(fd, buffer, sizeof buffer);
	} while (n < 0 && errno == EINTR);

	if (n <= 0)
		return (int) n;

	telnet_feed(telnet, buffer, (size_t) n);

	return 1;
}

static int send_pty(struct telnet *telnet, int fd) {
	unsigned char buffer[4096];
	ssize_t n;

	do {
		n = read(fd, buffer, sizeof buffer);
	} while (n < 0 && errno == EINTR);

	if (n <= 0)
		return (int) n;

	telnet_send_data(telnet, buffer, (size_t) n);

	return 1;
}

static int apply_winsize(struct termlet_state *state) {
	struct winsize ws = {
		.ws_col = state->width,
		.ws_row = state->height,
	};

	if (state->pty_master < 0)
		return 0;

	return ioctl(state->pty_master, TIOCSWINSZ, &ws);
}

static int start_child(struct termlet_state *state) {
	struct winsize ws;
	pid_t pid;
	int master;

	fprintf(stderr,
	        "ttype=%d binary-local=%d binary-peer=%d\n",
	        state->have_ttype,
	        telnet_negotiation_local(state->negotiation, TELNET_OPT_BINARY),
	        telnet_negotiation_peer(state->negotiation, TELNET_OPT_BINARY));

	if (state->state != SESSION_NEGOTIATING)
		return 0;

	/*
	 * TTYPE is required because TERM must be present in the environment
	 * before exec().
	 */
	if (!state->have_ttype)
		return 0;

	/*
	 * minitelnet only performs Telnet framing. It deliberately does not
	 * translate application data according to BINARY/NVT state, so do not
	 * start the byte-transparent PTY bridge until BINARY is active in both
	 * directions.
	 */
	if (telnet_negotiation_local(state->negotiation, TELNET_OPT_BINARY) !=
	        TELNET_OPTION_YES ||
	    telnet_negotiation_peer(state->negotiation, TELNET_OPT_BINARY) !=
	        TELNET_OPTION_YES)
		return 0;

	/*
	 * NAWS is optional. If it has not arrived yet, start with a
	 * conservative default. A later NAWS will resize the PTY.
	 */
	if (!state->have_naws) {
		state->width = DEFAULT_WIDTH;
		state->height = DEFAULT_HEIGHT;
	}

	memset(&ws, 0, sizeof ws);
	ws.ws_col = state->width;
	ws.ws_row = state->height;

	pid = forkpty(&master, NULL, NULL, &ws);

	if (pid < 0)
		return -1;

	if (pid == 0) {
		if (setenv("TERM", state->termname, 1) < 0) {
			perror("setenv TERM");
			_exit(127);
		}

		execvp(state->child_argv[0], state->child_argv);

		perror("execvp");
		_exit(127);
	}

	state->pty_master = master;
	state->child_pid = pid;
	state->state = SESSION_RUNNING;

	return 0;
}

static int finish_naws(struct termlet_state *state) {
	uint16_t width;
	uint16_t height;

	if (state->naws_overflow || state->naws_size != sizeof state->naws) {
		errno = EBADMSG;
		return -1;
	}

	width = ((uint16_t) state->naws[0] << 8) | state->naws[1];
	height = ((uint16_t) state->naws[2] << 8) | state->naws[3];

	/*
	 * RFC 1073 permits zero dimensions to mean "unspecified".
	 * Keep the previous/default value in that case.
	 */
	if (width != 0)
		state->width = width;

	if (height != 0)
		state->height = height;

	state->have_naws = 1;

	state->naws_size = 0;
	state->naws_overflow = 0;

	if (state->state == SESSION_RUNNING)
		return apply_winsize(state);

	return start_child(state);
}

static int feed_naws(struct termlet_state *state,
                     const unsigned char *buffer,
                     size_t size) {
	size_t remaining;
	size_t copy;

	if (buffer == NULL)
		return finish_naws(state);

	remaining = sizeof state->naws - state->naws_size;
	copy = size < remaining ? size : remaining;

	if (copy > 0) {
		memcpy(state->naws + state->naws_size, buffer, copy);
		state->naws_size += copy;
	}

	if (copy != size)
		state->naws_overflow = 1;

	return 0;
}

static int finish_ttype(struct termlet_state *state) {
	if (state->ttype_overflow ||
	    !state->ttype_have_command ||
	    state->ttype_size == 0 ||
	    state->ttype_command != TELNET_TTYPE_IS) {
		errno = EBADMSG;
		return -1;
	}

	state->termname[state->ttype_size] = '\0';
	state->have_ttype = 1;

	state->ttype_have_command = 0;
	state->ttype_size = 0;
	state->ttype_overflow = 0;

	return start_child(state);
}

static int feed_ttype(struct termlet_state *state,
                      const unsigned char *buffer,
                      size_t size) {
	size_t i = 0;

	if (buffer == NULL)
		return finish_ttype(state);

	if (!state->ttype_have_command) {
		if (size == 0)
			return 0;

		state->ttype_command = buffer[0];
		state->ttype_have_command = 1;
		i = 1;
	}

	if (i < size) {
		size_t payload = size - i;
		size_t remaining = (sizeof state->termname - 1) - state->ttype_size;
		size_t copy = payload < remaining ? payload : remaining;

		if (copy > 0) {
			memcpy(state->termname + state->ttype_size,
			       buffer + i,
			       copy);
			state->ttype_size += copy;
		}

		if (copy != payload)
			state->ttype_overflow = 1;
	}

	return 0;
}

static void send_transition(struct termlet_state *state,
                            const struct telnet_negotiation_transition *trns) {
	if (trns->outgoing != 0) {
		telnet_send_negotiation(&state->telnet,
		                        trns->outgoing,
		                        trns->option);
	}
}

static void request_option(struct termlet_state *state,
                           enum telnet_command command,
                           unsigned char option) {
	struct telnet_negotiation_transition trns;

	if (telnet_negotiation_request(state->negotiation,
	                               command,
	                               option,
	                               &trns))
		send_transition(state, &trns);
}

static enum telnet_command option_policy(enum telnet_command request,
                                         unsigned char option) {
	if (request == TELNET_CMD_WILL) {
		if (option == TELNET_OPT_TTYPE ||
		    option == TELNET_OPT_NAWS ||
		    option == TELNET_OPT_BINARY)
			return TELNET_CMD_DO;

		return TELNET_CMD_DONT;
	}

	if (request == TELNET_CMD_DO) {
		if (option == TELNET_OPT_BINARY)
			return TELNET_CMD_WILL;

		return TELNET_CMD_WONT;
	}

	return 0;
}

static void request_ttype(struct termlet_state *state) {
	static const unsigned char send[] = { TELNET_TTYPE_SEND };

	telnet_send_subnegotiation(&state->telnet,
	                           TELNET_OPT_TTYPE,
	                           send,
	                           sizeof send);
	telnet_send_subnegotiation_end(&state->telnet, TELNET_OPT_TTYPE);
}

static int handle_negotiation(struct termlet_state *state,
                              enum telnet_command command,
                              unsigned char option) {
	struct telnet_negotiation_transition trns;
	enum telnet_command response;
	int changed;

	changed = telnet_negotiation_feed(state->negotiation,
	                                  command,
	                                  option,
	                                  &trns);
	if (!changed)
		return 0;

	if (trns.error) {
		fprintf(stderr,
		        PROGRAM ": inconsistent telnet negotiation for option %u\n",
		        (unsigned int) option);
	}

	if (trns.state == TELNET_OPTION_REQUEST_PENDING) {
		response = option_policy(command, option);

		if (response != 0 &&
		    telnet_negotiation_respond(state->negotiation,
		                               response,
		                               option,
		                               &trns))
			send_transition(state, &trns);

		return 0;
	}

	send_transition(state, &trns);

	if (!trns.local && option == TELNET_OPT_TTYPE) {
		if (trns.state == TELNET_OPTION_YES) {
			request_ttype(state);
		} else if (trns.state == TELNET_OPTION_NO && !state->have_ttype) {
			fprintf(stderr,
			        PROGRAM ": remote terminal does not provide TTYPE\n");
			state->state = SESSION_DONE;
		}
	}

	if (option == TELNET_OPT_BINARY) {
		if (trns.state == TELNET_OPTION_NO) {
			fprintf(stderr,
			        PROGRAM ": remote terminal refused BINARY mode\n");
			state->state = SESSION_DONE;
		} else if (trns.state == TELNET_OPTION_YES) {
			return start_child(state);
		}
	}

	return 0;
}

static void telnet_handle(struct telnet *telnet,
                          const union telnet_event *ev,
                          void *userdata) {
	struct termlet_state *state = userdata;
	int rc = 0;

	(void) telnet;

	switch (ev->type) {
		case TELNET_EV_DATA:
			/*
			 * Terminal data is only meaningful once the PTY exists.
			 *
			 * In a well-behaved session TTYPE arrives during negotiation
			 * before application data starts flowing.
			 */
			if (state->state != SESSION_RUNNING)
				break;

			rc = write_all(state->pty_master,
			               ev->data.buffer,
			               ev->data.size);
			break;

		case TELNET_EV_SEND:
			rc = write_all(state->tcp_socket,
			               ev->data.buffer,
			               ev->data.size);
			break;

		case TELNET_EV_NEG:
			rc = handle_negotiation(state,
			                        ev->command.code,
			                        ev->neg.option);
			break;

		case TELNET_EV_SUBNEG:
			if (ev->subneg.option == TELNET_OPT_NAWS)
				rc = feed_naws(state,
				               ev->data.buffer,
				               ev->data.size);
			else if (ev->subneg.option == TELNET_OPT_TTYPE)
				rc = feed_ttype(state,
				                ev->data.buffer,
				                ev->data.size);
			break;

		case TELNET_EV_COMMAND:
			break;

		case TELNET_EV_ERROR:
			fprintf(stderr,
			        PROGRAM ": telnet protocol error: %d\n",
			        ev->error.code);
			state->state = SESSION_DONE;
			break;
	}

	if (rc < 0) {
		fprintf(stderr,
		        PROGRAM ": error while handling telnet event %d: %s\n",
		        ev->type,
		        strerror(errno));

		state->state = SESSION_DONE;
	}
}

static int handle_child_exit(struct termlet_state *state, int *exitcode) {
	int status;
	pid_t rc;

	if (state->child_pid < 0)
		return 0;

	rc = waitpid(state->child_pid, &status, WNOHANG);

	if (rc == 0)
		return 0;

	if (rc < 0)
		return -1;

	if (WIFEXITED(status))
		*exitcode = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		*exitcode = 128 + WTERMSIG(status);
	else
		*exitcode = 1;

	state->child_pid = -1;
	state->state = SESSION_DONE;

	return 1;
}

static int run_loop(struct termlet_state *state) {
	int exitcode = 0;

	while (state->state != SESSION_DONE) {
		struct pollfd fds[3];
		nfds_t nfds = 0;
		int idx_tcp = -1;
		int idx_pty = -1;
		int idx_stdin = -1;
		int rc;

		/* TCP is always active. */
		idx_tcp = (int) nfds;
		fds[nfds++] = (struct pollfd){
			.fd = state->tcp_socket,
			.events = POLLIN,
		};

		/*
		 * PTY and local stdin only exist as useful routes once the
		 * child is running.
		 */
		if (state->state == SESSION_RUNNING) {
			idx_pty = (int) nfds;
			fds[nfds++] = (struct pollfd){
				.fd = state->pty_master,
				.events = POLLIN,
			};

			idx_stdin = (int) nfds;
			fds[nfds++] = (struct pollfd){
				.fd = STDIN_FILENO,
				.events = POLLIN,
			};
		}

		rc = poll(fds, nfds, 250);

		if (rc < 0) {
			if (errno == EINTR)
				continue;

			fprintf(stderr,
			        PROGRAM ": poll failed: %s\n",
			        strerror(errno));
			return 2;
		}

		/* Check child even when poll timed out. */
		if (state->state == SESSION_RUNNING) {
			rc = handle_child_exit(state, &exitcode);

			if (rc < 0) {
				fprintf(stderr,
				        PROGRAM ": waitpid failed: %s\n",
				        strerror(errno));
				return 2;
			}

			if (state->state == SESSION_DONE)
				break;
		}

		if (idx_tcp >= 0 && fds[idx_tcp].revents != 0) {
			short revents = fds[idx_tcp].revents;

			if (revents & POLLIN) {
				rc = read_telnet(&state->telnet,
				                 state->tcp_socket);

				if (rc <= 0) {
					state->state = SESSION_DONE;
					continue;
				}
			}

			if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
				state->state = SESSION_DONE;
				continue;
			}
		}

		if (idx_pty >= 0 && fds[idx_pty].revents != 0) {
			short revents = fds[idx_pty].revents;

			if (revents & POLLIN) {
				rc = send_pty(&state->telnet,
				              state->pty_master);

				if (rc <= 0) {
					state->state = SESSION_DONE;
					continue;
				}
			}

			if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
				/*
				 * PTYs commonly report HUP when the child exits.
				 * Let waitpid() determine the actual exit status.
				 */
			}
		}

		if (idx_stdin >= 0 && fds[idx_stdin].revents != 0) {
			short revents = fds[idx_stdin].revents;

			if (revents & POLLIN) {
				char buffer[4096];
				ssize_t n;

				do {
					n = read(STDIN_FILENO,
					         buffer,
					         sizeof buffer);
				} while (n < 0 && errno == EINTR);

				if (n > 0) {
					if (write_all(state->pty_master,
					              buffer,
					              (size_t) n) < 0) {
						fprintf(stderr,
						        PROGRAM ": writing stdin to PTY failed: %s\n",
						        strerror(errno));
						state->state = SESSION_DONE;
					}
				}
			}
		}
	}

	/*
	 * If the session disappeared while the child was still alive,
	 * terminate it for now.
	 *
	 * A later version could make this policy configurable.
	 */
	if (state->child_pid > 0) {
		int status;

		kill(state->child_pid, SIGHUP);

		if (waitpid(state->child_pid, &status, 0) > 0) {
			if (WIFEXITED(status))
				exitcode = WEXITSTATUS(status);
			else if (WIFSIGNALED(status))
				exitcode = 128 + WTERMSIG(status);
		}
	}

	return exitcode;
}

int main(int argc, char *argv[]) {
	const char *port = "23";
	const char *hostname;
	int exitcode;

	struct termlet_state state = {
		.state = SESSION_NEGOTIATING,

		.tcp_socket = -1,
		.pty_master = -1,
		.child_pid = -1,

		.width = DEFAULT_WIDTH,
		.height = DEFAULT_HEIGHT,
	};

	ARGBEGIN
	switch (OPT) {
		case 'p':
			port = EARGF(usage(1));
			break;

		default:
			fprintf(stderr,
			        PROGRAM ": unknown option '-%c'\n",
			        OPT);
			usage(1);
	}
	ARGEND

	if (argc < 2) {
		fprintf(stderr,
		        PROGRAM ": insufficient arguments\n");
		usage(1);
	}

	hostname = argv[0];
	SHIFT;

	state.child_argv = argv;

	state.tcp_socket = create_socket(hostname, port);

	if (state.tcp_socket < 0)
		return 2;

	telnet_init(&state.telnet, telnet_handle, &state);

	request_option(&state, TELNET_CMD_DO, TELNET_OPT_TTYPE);
	request_option(&state, TELNET_CMD_DO, TELNET_OPT_NAWS);
	request_option(&state, TELNET_CMD_WILL, TELNET_OPT_BINARY);
	request_option(&state, TELNET_CMD_DO, TELNET_OPT_BINARY);

	exitcode = run_loop(&state);

	if (state.pty_master >= 0)
		close(state.pty_master);

	if (state.tcp_socket >= 0)
		close(state.tcp_socket);

	return exitcode;
}
