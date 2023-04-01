#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include "protocol.h"

int32_t read_full(int fd, char *buf, size_t n) {
        while (n > 0) {
                ssize_t rv = read(fd, buf, n); // read can read only 'partially' (not what we expect), so we keep trying until we read n bytes
                std::cerr << "rv is " << rv << " n is " << n << std::endl;
                if (rv <= 0) {
                        return -1; // did not read enough bytes to properly eof
                }
                if ((size_t) rv > n) { // panic if we read more than n bytes
                        std::cerr << "invalid read of size " << (size_t) rv << " with max buffer of " << n << std::endl;
                        exit(1);
                };
                n -= (size_t) rv;
                buf += rv;
        }
        return 0;
}

int32_t write_all(int fd, const char *buf, size_t n) {
        while (n > 0) {
                ssize_t rv = write(fd, buf, n); // write can only write partially, so we try until we write all in the buffer
                if (rv <= 0) {
                        std::cerr << "returning -1 from write_all" << std::endl;
                        return -1;
                }
                if ((size_t) rv > n) { 
                        std::cerr << "invalid write of size " << (size_t) rv << " with max buffer of " << n << std::endl;
                        exit(1);
                };
                n -= (size_t) rv;
                buf += rv;
        }
        std::cerr << "returning 0 from write_all" << std::endl;
        return 0;
}

