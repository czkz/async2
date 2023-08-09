#pragma once
#include "poll_loop.h"
#include "posix_wrappers.h"
#include "stream.h"
#include "socket.h"

namespace async::msg_transport {
    class udp_socket {
    public:
        explicit udp_socket(c_api::fd fd_handle) : fd_handle(std::move(fd_handle)) {}

        static constexpr size_t max_incoming_packet_size = 65536;
        static constexpr size_t max_outgoing_packet_size = 65536;

        task<void> wait_read() { co_await poll_loop.wait_read(fd_handle); }
        task<void> wait_write() { co_await poll_loop.wait_write(fd_handle); }
        size_t read(void* buf, size_t size) { return c_api::read(fd_handle, buf, size); }
        size_t write(std::string_view data) { return c_api::write(fd_handle, data); }
        task<void> close() { c_api::close(fd_handle); co_return; }

        static constexpr bool has_lookahead = true;
        size_t available_bytes() { return c_api::available_bytes(fd_handle); }

        c_api::fd fd_handle;
    };
}

namespace async::udp::detail {
    // This is just to avoid recirsive includes, connect_udp also won't do lookup when passed an ip address.
    inline task<msg_transport::udp_socket> connect_udp_nolookup(std::string_view ip, uint16_t port) {
        c_api::fd fd = co_await async::detail::make_connected_socket(ip, port, SOCK_DGRAM, IPPROTO_UDP);
        co_return msg_transport::udp_socket(std::move(fd));
    }
}
