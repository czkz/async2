#pragma once
#include "udp_raw.h"
#include "dns.h"

namespace async::udp {
    inline task<msgstream<msg_transport::udp_socket>> connect(std::string_view host, uint16_t port) {
        std::string ip = co_await dns::host_to_ip(host);
        co_return co_await(detail::connect_udp_nolookup(ip, port));
    }
}
