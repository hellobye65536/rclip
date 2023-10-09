#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <getopt.h>

const char *program_name = "rclip";

void usage() {
	printf(
			"Usage %s [options] <copy port> <paste port>\n"
			"\n"
			"Arguments:\n"
			"  <copy port>                    when connecting to this port, send the new clipboard\n"
			"  <paste port>                   when connecting to this port, recieve the current clipboard\n"
			"\n"
			"Options:\n"
			"  -h, --help                     display this help\n"
			"  -c, --copy                     when copying, use this command instead of wl-clipboard/xclip\n"
			"  -p, --paste                    when pasting, use this command instead of wl-clipboard/xclip\n"
			"  -a, --address <ipv4 address>   bind to this address instead of all addresses\n",
			program_name);
}

int parse_port(const char *port_s) {
	char *end_ptr = NULL;
	const int port = strtol(port_s, &end_ptr, 10);

	if (errno != 0 ||
			(end_ptr != NULL && end_ptr[0] != '\0') ||
			port <= 0 || 65536 <= port) {
		printf("invalid port: %s", port_s);
		return -1;
	}
	
	return port;
}

int make_sock(const char *name, const struct in_addr addr, const int port) {
	const int socket_r = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (socket_r < 0) {
		char buf[32];
		snprintf(buf, sizeof(buf) / sizeof(char), "error creating %s", name);
		perror(buf);
		return -1;
	}

	const struct sockaddr_in r_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
		.sin_addr = addr,
	};

	const int bind_r = bind(socket_r, (struct sockaddr *) &r_addr, sizeof(r_addr));
	if (bind_r < 0) {
		char buf[32];
		snprintf(buf, sizeof(buf) / sizeof(char), "error binding %s", name);
		perror(buf);
		return -1;
	}

	const int listen_r = listen(socket_r, 4);
	if (listen_r < 0) {
		char buf[32];
		snprintf(buf, sizeof(buf) / sizeof(char), "error listening on %s", name);
		perror(buf);
		return -1;
	}

	return socket_r;
}

int main(const int argc, char *const *argv) {
	if (argc >= 1)
		program_name = argv[0];

	if (argc == 1) {
		usage();
		return 0;
	}

	static const char *options = "hc:p:a:";
	static const struct option long_options[] = {
		{"help",    no_argument,       NULL, 'h'},
		{"copy",    required_argument, NULL, 'c'},
		{"paste",   required_argument, NULL, 'p'},
		{"address", required_argument, NULL, 'a'},
		{0,         0,                 0,     0 }
	};

	const char *copy_command = NULL;
	const char *paste_command = NULL;

	char *env_tmp;
	if ((env_tmp = getenv("WAYLAND_DISPLAY")) != NULL && env_tmp[0] != '\0') {
		copy_command = "wl-copy";
		paste_command = "wl-paste";
	} else if ((env_tmp = getenv("DISPLAY")) != NULL && env_tmp[0] != '\0') {
		copy_command = "xclip -selection clipboard";
		paste_command = "xclip -out -selection clipboard";
	}

	struct in_addr bind_addr = { .s_addr = htonl(INADDR_ANY) };

	while (true) {
		const int c = getopt_long(argc, argv, options, long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
			case 'h':
				usage();
				return 0;
			case 'c':
				copy_command = optarg;
				break;
			case 'p':
				paste_command = optarg;
				break;
			case 'a':
				if (inet_aton(optarg, &bind_addr) == 0) {
					fprintf(stderr, "invalid address: %s", optarg);
					return 1;
				}
				break;
			case '?':
				return 1;
			// should never get here
			default:
				abort();
		}
	}

	if (argc - optind < 2 || copy_command == NULL || paste_command == NULL) {
		fprintf(stderr, "missing arguments");
		usage();
		return 1;
	}

	const int copy_port = parse_port(argv[optind + 0]);
	if (copy_port < 0)
		return 1;

	const int paste_port = parse_port(argv[optind + 1]);
	if (paste_port < 0)
		return 1;

	const int copy_socket = make_sock("copy_socket", bind_addr, copy_port);
	if (copy_socket < 1)
		return 2;

	const int paste_socket = make_sock("paste_socket", bind_addr, paste_port);
	if (paste_socket < 1)
		return 2;

	const int devnull_fd = open("/dev/null", O_RDWR | O_CLOEXEC);

	struct pollfd poll_fds[] = {
		{
			.fd = copy_socket,
			.events = POLLIN,
			.revents = 0,
		},
		{
			.fd = paste_socket,
			.events = POLLIN,
			.revents = 0,
		},
	};

	while (true) {
		const int poll_r = poll(poll_fds, sizeof(poll_fds) / sizeof(struct pollfd), -1);
		if (poll_r < 0) {
			if (errno == EINTR)
				continue;
			perror("error polling");
			return 2;
		}

		for (int i = 0; i < 2; i++) {
			if (!(poll_fds[i].revents | POLLIN))
				continue;

			while (true) {
				const int accept_r = accept4(poll_fds[i].fd, NULL, NULL, SOCK_CLOEXEC);

				if (accept_r < 0) {
					if (errno == EAGAIN)
						break;

					perror("error accepting");
					switch (errno) {
						case ENETDOWN: case EPROTO: case ENOPROTOOPT: case EHOSTDOWN:
						case ENONET: case EHOSTUNREACH: case ENETUNREACH:
							continue;
						default:
							perror("error accepting");
							return 2;
					}
				}

				const pid_t fork_r = fork();
				if (fork_r < 0) {
					perror("error forking");
					return 2;
				}

				if (fork_r == 0) {
					if (i == 0) {
						shutdown(accept_r, SHUT_WR);

						dup2(accept_r, 0);
						dup2(devnull_fd, 1);
						dup2(devnull_fd, 2);

						execl("/bin/sh", "sh", "-c", copy_command, (char *) NULL);
					} else if (i == 1) {
						shutdown(accept_r, SHUT_RD);

						dup2(devnull_fd, 0);
						dup2(accept_r, 1);
						dup2(devnull_fd, 2);

						execl("/bin/sh", "sh", "-c", paste_command, (char *) NULL);
					}

					abort();
				}

				close(accept_r);
			}
		}

		poll_fds[0].revents = 0;
		poll_fds[1].revents = 0;
	}

	return 0;
}
