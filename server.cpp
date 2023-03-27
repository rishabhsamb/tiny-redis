#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>

static void do_something(int connfd) {
	char rbuf[64] = {};
	ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
	if (n < 0) {
		std::cerr << "fail on read()" << std::endl;
		return;
	}
	printf("client says %s\n", rbuf);
	char wbuf[] = "world";
	write(connfd, wbuf, strlen(wbuf));
}

int main() {
	// get fd for socket on ipv4, for TCP/IP connections
	// 0 specifies the 'default' protocol for sockets over ipv4
	// which is TCP/IP
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int val = 1;
	// set socket options
	// SOL_SOCKET specifies that the type of option being applied
	// SO_REUSEADDR is a protocol independent option
	// SO_REUSEADDR is an option that specifies we want to be able
	// to restart onto the address being used right after this program
	// discards the address
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));


	// set up bind config and attempt a bind
	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = ntohs(1234);
	addr.sin_addr.s_addr = ntohl(0);
	int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
	if (rv) {
		std::cerr << "fail on bind()\n" << std::endl;
		exit(1); // if rv comes back non-zero, the operation failed so we exit ('die') on bind
	}

	// attempt a listen after the bind
	rv = listen(fd, SOMAXCONN);
	if (rv) {
		std::cerr << "fail on listen()\n" << std::endl;
		exit(1); // if rv comes back non-zero, the operation failed so we exit ('die') on listen
	}

	// we are now listening on socket
	while (true) {
		struct sockaddr_in client_addr = {};
		socklen_t socklen = sizeof(client_addr);
		int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
		if (connfd < 0) {
			continue; // could not accept connection
		}

		do_something(connfd);
		close(connfd);
	}
}


