#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <vector>
#include <map>
#include <poll.h>
#include "../headers/protocol.h"
#include "../headers/conn.h"

enum {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

static std::map<std::string, std::string> g_map;

static uint32_t do_get(
    const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    if (!g_map.count(cmd[1])) {
        return RES_NX;
    }
    std::string &val = g_map[cmd[1]];
    // make sure value isn't bigger than max message size
    memcpy(res, val.data(), val.size());
    *reslen = (uint32_t)val.size();
    return RES_OK;
}

static uint32_t do_set(
    const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    (void)res;
    (void)reslen;
    g_map[cmd[1]] = cmd[2];
    return RES_OK;
}

static uint32_t do_del(
    const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    (void)res;
    (void)reslen;
    g_map.erase(cmd[1]);
    return RES_OK;
}

static int32_t parse_req(
    const uint8_t *data, size_t len, std::vector<std::string> &out)
{
    if (len < 4) {
        return -1;
    }
    uint32_t n = 0;
    memcpy(&n, &data[0], 4);
    if (n > 1024) {
        return -1;
    }

    size_t pos = 4;
    while (n--) {
        if (pos + 4 > len) {
            return -1;
        }
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if (pos + 4 + sz > len) {
            return -1;
        }
        out.push_back(std::string((char *)&data[pos + 4], sz));
        pos += 4 + sz;
    }

    if (pos != len) {
        return -1;  // trailing garbage
    }
    return 0;
}
static bool try_flush_buffer(Conn *conn) {
	ssize_t rv = 0;
	do {
		size_t remain = conn->wbuf_size - conn->wbuf_sent;
		rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
	} while (rv < 0 && errno == EINTR);
	if (rv < 0 && errno == EAGAIN) {
		// got EAGAIN, stop.
		return false;
	}
	if (rv < 0) {
		std::cerr << "write() error" << std::endl;
		conn->state = STATE_END;
		return false;
	}
	conn->wbuf_sent += (size_t)rv;
	// ensure we have sent at most what we can hold
	if (conn->wbuf_sent == conn->wbuf_size) {
		// response was fully sent, change state back
		conn->state = STATE_REQ;
		conn->wbuf_sent = 0;
		conn->wbuf_size = 0;
		return false;
	}
	// still got some data in wbuf, could try to write again
	return true;
}

static void state_res(Conn *conn) {
	while (try_flush_buffer(conn)) {}
}

static int32_t do_request(
    const uint8_t *req, uint32_t reqlen,
    uint32_t *rescode, uint8_t *res, uint32_t *reslen)
{
    std::vector<std::string> cmd;
    if (0 != parse_req(req, reqlen, cmd)) {
	std::cerr << "error when parsing request()" << std::endl;
	return -1;
    }
    if (cmd.size() == 2 && cmd[0] == "get") {
        *rescode = do_get(cmd, res, reslen);
    } else if (cmd.size() == 3 && cmd[0] == "set") {
        *rescode = do_set(cmd, res, reslen);
    } else if (cmd.size() == 2 && cmd[0] == "del") {
        *rescode = do_del(cmd, res, reslen);
    } else {
        // cmd is not recognized
        *rescode = RES_ERR;
        const char *msg = "Unknown cmd";
        strcpy((char *)res, msg);
        *reslen = strlen(msg);
        return 0;
    }
    return 0;
}



static bool try_one_request(Conn *conn) {
	// try to parse a request from the buffer
	if (conn->rbuf_size < 4) {
		return false;
	}
	uint32_t len = 0;
	memcpy(&len, &conn->rbuf[0], 4);
	if (len > k_max_msg) {
		std::cerr << "too long" << std::endl;
		conn->state = STATE_END;
		return false;
	}
	if (4 + len > conn->rbuf_size) {
		return false;
	}

	////  printf("client says: %.*s\n", len, &conn->rbuf[4]);

	uint32_t rescode = 0;
	uint32_t wlen = 0;
	int32_t err = do_request(&conn->rbuf[4], len, &rescode, &conn->wbuf[4+4], &wlen);
	if (err) {
		conn->state = STATE_END;
		return false;
	}
	wlen += 4;
	memcpy(&conn->wbuf[0], &len, 4);
	memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
	conn->wbuf_size = 4 + len;

	// remove the request from the buffer.
	size_t remain = conn->rbuf_size - 4 - len;
	if (remain) {
		memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
	}
	conn->rbuf_size = remain;

	// change state
	conn->state = STATE_RES;
	state_res(conn);

	return (conn->state == STATE_REQ);
}

static void fd_set_nb(int fd) {
	errno = 0;
	int flags = fcntl(fd, F_GETFL, 0);
	if (errno) {
		std::cerr << "error on fcntl()" << std::endl;
		return;
	}
	flags |= O_NONBLOCK;
	errno = 0;
	(void) fcntl(fd, F_SETFL, flags);
	if (errno) {
		std::cerr << "error on fcntl()" << std::endl;
	}
}

static void conn_put(std::vector<Conn*>& fd2conn, struct Conn* conn)  {
	if (fd2conn.size() <= (size_t) conn->fd) {
		fd2conn.resize(conn->fd + 1); // this fixes any extra space in the vector existing (why not just use a hash map?)
	}
	fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn*>& fd2conn, int fd) {
	struct sockaddr_in client_addr = {};
	socklen_t socklen = sizeof(client_addr);
	int connfd = accept(fd, (struct sockaddr*) &client_addr, &socklen);
	if (connfd < 0) {
		std::cerr << "error on accept()" << std::endl;
		return -1;
	}
	fd_set_nb(connfd);
	struct Conn* conn = (struct Conn*) malloc(sizeof(struct Conn));
	if (!conn) { // failed to malloc
		close(connfd);
		return -1;
	}
	conn->fd = connfd;
	conn->state = STATE_REQ;
	conn->rbuf_size = 0;
	conn->wbuf_size = 0;
	conn->wbuf_sent = 0;
	conn_put(fd2conn, conn);
	return 0;
}

static bool try_fill_buffer(Conn* conn) {
	// assume that rbuf_size < sizeof(rbuf)
	ssize_t rv = 0;
	do {
		size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
		rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
	} while (rv < 0 && errno == EINTR);
	if (rv < 0) {
		std::cerr << "error on read() in try_fill_buffer" << std::endl;
		conn->state = STATE_END;
		return false;
	}
	if (rv == 0) {
		if (conn->rbuf_size > 0) {
			std::cerr << "unexpected EOF in try_fill_buffer" << std::endl;
			} else {
			std::cerr << "EOF" << std::endl;
		}
		conn->state = STATE_END;
		return false;
	}
	conn->rbuf_size += (size_t) rv;
	// there's an assertion here that expects the sizeof(rbuf) to be at least twice rbuf_size...unsure why

	while (try_one_request(conn)) {};
	return conn->state == STATE_REQ;
}

static void state_req(Conn *conn) {
	while (try_fill_buffer(conn)) {}
}

static void connection_io(Conn *conn) {
	if (conn->state == STATE_REQ) {
	state_req(conn);
		} else if (conn->state == STATE_RES) {
	state_res(conn);
	} else {
		std::cerr << "got state that wasn't req or res " << conn->state << ", exiting" << std::endl;
		exit(1);
	}
}

int main() {
	// get fd for socket on ipv4, for TCP/IP connections
	// 0 specifies the 'default' protocol for sockets over ipv4
	// which is TCP/IP
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		std::cerr << "fail on socket()" << std::endl;
		exit(1);	
	}
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
		std::cerr << "fail on bind()" << std::endl;
		exit(1); // if rv comes back non-zero, the operation failed so we exit ('die') on bind
	}

	// attempt a listen after the bind
	rv = listen(fd, SOMAXCONN);
	if (rv) {
		std::cerr << "fail on listen()" << std::endl;
		exit(1); // if rv comes back non-zero, the operation failed so we exit ('die') on listen
	}

	std::vector<Conn*> fd2conn;
	fd_set_nb(fd);

	// we are now listening on socket
	std::vector<struct pollfd> poll_args;
	while (true) {
		poll_args.clear();
		struct pollfd pfd = {fd, POLLIN, 0};
		poll_args.push_back(pfd);

		for (Conn* conn : fd2conn) {
			if (!conn) {
				continue;
			}
			struct pollfd pfd = {};
			pfd.fd = conn->fd;
			pfd.events = conn->state == STATE_REQ ? POLLIN : POLLOUT;
			pfd.events = pfd.events | POLLERR;
			poll_args.push_back(pfd);
		}

		int rv = poll(poll_args.data(), (nfds_t) poll_args.size(), 1000); // 1000 is arbitrary timeout
		if (rv < 0) {
			std::cerr << "error on poll()" << std::endl;
			exit(1);
		}

		for (size_t i = 1; i < poll_args.size(); ++i) {
			if (poll_args[i].revents) {
				Conn* conn = fd2conn[poll_args[i].fd];
				connection_io(conn); // process connection
				if (conn->state == STATE_END) {
					fd2conn[conn->fd] = NULL; // no dangling ptr
					(void) close(conn->fd);
					free(conn);
				}
			}
		}

		if (poll_args[0].revents) {
			(void) accept_new_conn(fd2conn, fd);
		}
	}
	return 0;
}


