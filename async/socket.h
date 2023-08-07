#pragma once
#include "coro.h"
#include "poll_loop.h"
#include "posix_wrappers.h"

namespace async::detail {
    inline task<c_api::fd> make_connected_socket(std::string_view ip, uint16_t port, int type, int protocol) {
        c_api::fd fd = c_api::socket(AF_INET, type, protocol);
        sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(port),
            .sin_addr = c_api::inet_pton(AF_INET, ip),
            .sin_zero = {},
        };
        int res = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (res == -1) {
            if (errno != EINPROGRESS) {
                throw ex::fn("connect()", strerror(errno));
            }
            co_await poll_loop.wait_write(fd);
            int err = c_api::getsockopt(fd, SOL_SOCKET, SO_ERROR);
            if (err != 0) {
                throw ex::fn("connect()", strerror(err));
            }
        }
        co_return fd;
    }
}
