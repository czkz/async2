#pragma once
#include <arpa/inet.h>
#include <cassert>
#include <unistd.h>
#include <netdb.h>
#include <ex.h>
#include <memory>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <sys/ioctl.h>

// All functions are nonblocking and expect a nonblocking socket
namespace async::posix::c_api {
    struct eof : std::exception {
        virtual const char* what() const noexcept override { return "end of stream"; }
    };

    // Returns number of bytes written (may be zero)
    inline size_t write(int fd, std::string_view data) {
        ssize_t n_sent = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (n_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        } else if (n_sent == -1 && errno == EPIPE) {
            throw eof();
        }
        ex::wrape(n_sent, "send()");
        return n_sent;
    }
    // Returns number of bytes read (may be zero)
    inline size_t read(int fd, void* buf, size_t size) {
        ssize_t n_read = ::recv(fd, buf, size, MSG_NOSIGNAL);
        if (n_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        } else if (n_read == 0) {
            throw eof();
        }
        ex::wrape(n_read, "recv()");
        return n_read;
    }
    inline void fcntl(int fd, int cmd, int arg) {
        ex::wrape(::fcntl(fd, cmd, arg), "fcntl()");
    }
    [[nodiscard]]
    inline size_t available_bytes(int fd) {
        int value;
        ex::wrape(::ioctl(fd, FIONREAD, &value), "ioctl()");
        return value;
    }
    inline void setsockopt(int fd, int level, int optname, int optval) {
        ex::wrape(::setsockopt(fd, level, optname, &optval, sizeof(optval)), "setsockopt()");
    }
    [[nodiscard]]
    inline int getsockopt(int fd, int level, int optname) {
        int optval;
        socklen_t optlen = sizeof(optval);
        ex::wrape(::getsockopt(fd, level, optname, &optval, &optlen), "getsockopt()");
        assert(optlen == sizeof(optval));
        return optval;
    }
    // Returns a non-blocking socket
    [[nodiscard]]
    inline int socket(int domain, int type, int protocol) {
        int fd = ex::wrape(::socket(domain, type, protocol), "socket()");
        c_api::fcntl(fd, F_SETFL, O_NONBLOCK);
        return fd;
    }
    [[nodiscard]]
    inline in_addr inet_aton(const char* ip) {
        in_addr ret;
        ex::wrapb(::inet_aton(ip, &ret), "inet_aton()");
        return ret;
    }
    // Returns a non-blocking socket, ip may be nullptr for INADDR_ANY
    [[nodiscard]]
    inline int bind_listen(const char* ip, uint16_t port) {
        int fd = c_api::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        in_addr ia = ip ? c_api::inet_aton(ip) : in_addr{INADDR_ANY};
        sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(port),
            .sin_addr = ia,
            .sin_zero = {},
        };
        ex::wrape(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), "bind()");
        ex::wrape(::listen(fd, 256), "listen()");
        return fd;
    }
    // Returns accepted socket or -1
    [[nodiscard]]
    inline int accept(int fd) {
        int client = ::accept(fd, nullptr, nullptr);
        if (client == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return -1;
        }
        ex::wrape(client, "accept()");
        c_api::fcntl(client, F_SETFL, O_NONBLOCK);
        return client;
    }
}

// namespace async::posix {
//     // Reusable
//     class ConnectionCreator {
//         using addrinfo_handle = std::unique_ptr<
//             std::remove_pointer<addrinfo>::type,
//             decltype(&freeaddrinfo)>;
//         static constexpr addrinfo hints {
//             .ai_flags = 0,
//             .ai_family = AF_UNSPEC,      // Allow IPv4 or IPv6
//             .ai_socktype = SOCK_STREAM,
//             .ai_protocol = IPPROTO_TCP,
//             .ai_addrlen = 0,
//             .ai_addr = nullptr,
//             .ai_canonname = nullptr,
//             .ai_next = nullptr,
//         };
//
//         addrinfo_handle ai_list {nullptr, freeaddrinfo};
//         addrinfo* ai;
//         int saved_err = 0;
//         int sfd = -1;
//     public:
//         ConnectionCreator() = default;
//         ConnectionCreator(const HostInfo& info) { start(info); }
//
//         void start(const HostInfo& info) {
//             addrinfo* ai_tmp;
//             int res = getaddrinfo(info.ip.c_str(), info.port.c_str(), &hints, &ai_tmp);
//             if (res != 0) {
//                 throw ex::runtime(fmt("getaddrinfo:", gai_strerror(res)));
//             }
//             ai_list.reset(ai_tmp);
//             ai = ai_list.get();
//             sfd = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK, ai->ai_protocol);
//             ex::wrape(sfd, "socket()");
//         }
//
//         int think() {
//             if (ai_list == nullptr) { return sfd; }
//             while (true) {
//                 if (connect(sfd, ai->ai_addr, ai->ai_addrlen) != -1) {
//                     ai_list.reset();
//                     return sfd;
//                 } else if (errno == EAGAIN || errno == EALREADY || errno == EINPROGRESS) {
//                     return -1;
//                 } else {
//                     saved_err = errno;
//                     close(sfd);
//                     ai = ai->ai_next;
//                     if (ai == NULL) {
//                         throw ex::fn("connect()", strerror(saved_err));
//                     }
//                     sfd = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK, ai->ai_protocol);
//                     ex::wrape(sfd, "socket()");
//                 }
//             }
//         }
//     };
// }
