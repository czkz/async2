#pragma once
#include "stream.h"
#include "dns.h"

namespace async {
    class server {
    public:
        task<stream<provider::fd>> accept() {
            co_await poll_loop.wait_read(server_fd);
            co_return stream(provider::fd(c_api::accept(server_fd)));
        }
        static server from_fd(c_api::fd fd) { return server{std::move(fd)}; }
    private:
        explicit server(c_api::fd fd) : server_fd(std::move(fd)) {}
    private:
        c_api::fd server_fd;
    };
}

namespace async {
    inline task<stream<provider::fd>> connect(std::string_view host, uint16_t port) {
        std::string ip = co_await dns::host_to_ip(host);
        c_api::fd fd = co_await detail::make_connected_socket(ip, port, SOCK_STREAM, IPPROTO_TCP);
        co_return stream(provider::fd(std::move(fd)));
    }

    inline task<server> listen(std::string_view ip, uint16_t port) {
        co_return server::from_fd(c_api::bind_listen(ip, port));
    }
}
