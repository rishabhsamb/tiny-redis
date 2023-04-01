#ifndef CONN_H
#define CONN_H

#include <cstdint>
#include "protocol.h"

enum {
	STATE_REQ = 0,
	STATE_RES = 1,
	STATE_END = 2
};

struct Conn {
	int fd = -1;
	uint32_t state = 0;
	
	size_t rbuf_size = 0;
	uint8_t rbuf[4 + k_max_msg];

	size_t wbuf_size = 0;
	size_t wbuf_sent = 0;
	uint8_t wbuf[4 + k_max_msg];
};

#endif
