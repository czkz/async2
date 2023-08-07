#pragma once
#include "stream.h"
#include "dns.h"

namespace async::transport {
    class tcp_socket {
    public:
        explicit tcp_socket(c_api::fd fd_handle) : fd_handle(std::move(fd_handle)) {}

        task<void> wait_read() { co_await poll_loop.wait_read(fd_handle); }
        task<void> wait_write() { co_await poll_loop.wait_write(fd_handle); }
        size_t read(void* buf, size_t size) { return c_api::read(fd_handle, buf, size); }
        size_t write(std::string_view data) { return c_api::write(fd_handle, data); }

        task<void> flush() {
            // Assuming TCP_CORK is set
            c_api::setsockopt(fd_handle, IPPROTO_TCP, TCP_NODELAY, 1);
            co_return;
        }
        task<void> close() {
            c_api::close(fd_handle);
            co_return;
        }

        static constexpr bool has_lookahead = true;
        size_t available_bytes() { return c_api::available_bytes(fd_handle); }

    private:
        c_api::fd fd_handle;
    };
}

namespace async {
    class server {
    public:
        task<stream<transport::tcp_socket>> accept() {
            co_await poll_loop.wait_read(server_fd);
            co_return stream(transport::tcp_socket(c_api::accept(server_fd)));
        }
        static server from_fd(c_api::fd fd) { return server{std::move(fd)}; }
    private:
        explicit server(c_api::fd fd) : server_fd(std::move(fd)) {}
    private:
        c_api::fd server_fd;
    };

    inline task<stream<transport::tcp_socket>> connect(std::string_view host, uint16_t port) {
        std::string ip = co_await dns::host_to_ip(host);
        c_api::fd fd = co_await detail::make_connected_socket(ip, port, SOCK_STREAM, IPPROTO_TCP);
        c_api::setsockopt(fd, IPPROTO_TCP, TCP_CORK, 1);
        co_return stream(transport::tcp_socket(std::move(fd)));
    }

    inline task<server> listen(std::string_view ip, uint16_t port) {
        co_return server::from_fd(c_api::bind_listen(ip, port));
    }
}
