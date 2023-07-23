#pragma once
#include "poll_loop.h"
#include "posix_wrappers.h"
#include "stream.h"

namespace async::provider {
    class udp_socket {
    public:
        explicit udp_socket(c_api::fd fd_handle) : fd_handle(std::move(fd_handle)) {}

        static constexpr bool stream_oriented = false;
        static constexpr bool has_lookahead = true;

    protected:
        task<void> wait_read() { co_await poll_loop.wait_read(fd_handle); }
        task<void> wait_write() { co_await poll_loop.wait_write(fd_handle); }

        size_t read(void* buf, size_t size) { return c_api::read(fd_handle, buf, size); }
        size_t write(std::string_view data) { return c_api::write(fd_handle, data); }

        task<void> close() { c_api::close(fd_handle); co_return; }

        size_t available_bytes() { return c_api::available_bytes(fd_handle); }

        constexpr size_t packet_size() const { return 65536; }

    private:
        c_api::fd fd_handle;
    };
}

namespace async::detail {
    inline task<msgstream<provider::udp_socket>> connect_udp_nolookup(std::string_view ip, uint16_t port) {
        c_api::fd fd = co_await detail::make_connected_socket(std::string(ip).c_str(), port, SOCK_DGRAM, IPPROTO_UDP);
        co_return msgstream(provider::udp_socket(std::move(fd)));
    }
}
