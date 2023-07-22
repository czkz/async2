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
namespace async::c_api {
    struct eof : std::exception {
        virtual const char* what() const noexcept override { return "end of stream"; }
    };

    // Transparent RAII wrapper around a file descriptor
    class fd {
        int value = -1;
    public:
        explicit fd(int fd) noexcept : value(fd) {}
        fd(const fd&) = delete;
        fd(fd&& o) noexcept { std::swap(value, o.value); }
        ~fd() noexcept { if (value != -1) { ::close(value); } }
        operator int() const& { return value; }
        operator int() && = delete;
        explicit operator bool() const& { return value != -1; }
        int release() { int ret = -1; std::swap(ret, value); return ret; }
    };

    // Returns number of bytes written (may be zero)
    inline size_t write(int fd, std::string_view data) {
        ssize_t n_sent = ::write(fd, data.data(), data.size());
        if (n_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        } else if (n_sent == -1 && errno == EPIPE) {
            throw eof();
        }
        ex::wrape(n_sent, "write()");
        return n_sent;
    }
    // Returns number of bytes read (may be zero)
    inline size_t read(int fd, void* buf, size_t size) {
        ssize_t n_read = ::read(fd, buf, size);
        if (n_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        } else if (n_read == 0) {
            throw eof();
        }
        ex::wrape(n_read, "read()");
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
    inline fd socket(int domain, int type, int protocol) {
        c_api::fd fd (ex::wrape(::socket(domain, type, protocol), "socket()"));
        c_api::fcntl(fd, F_SETFL, O_NONBLOCK);
        return fd;
    }
    // Returns a non-blocking fd
    [[nodiscard]]
    inline fd open(const char* pathname, int flags, mode_t mode = 00666) {
        c_api::fd fd {ex::wrape(::open(pathname, flags, mode), "open()")};
        c_api::fcntl(fd, F_SETFL, O_NONBLOCK);
        return fd;
    }
    // Returns a non-blocking fd
    [[nodiscard]]
    inline fd creat(const char* pathname, mode_t mode = 00666) {
        c_api::fd fd {ex::wrape(::creat(pathname, mode), "creat()")};
        c_api::fcntl(fd, F_SETFL, O_NONBLOCK);
        return fd;
    }
    // Optional, use to close a descriptor early
    inline void close(fd& fd) noexcept {
        ::close(fd.release());
    }
    [[nodiscard]]
    inline in_addr inet_pton(int af, const char* ip) {
        in_addr ret;
        int res = ::inet_pton(af, ip, &ret);
        if (res == 0) {
            throw ex::fn("inet_pton()");
        } else if (res == -1) {
            throw ex::fn("inet_pton()", strerror(errno));
        }
        return ret;
    }
    [[nodiscard]]
    inline std::string inet_ntop(int af, in_addr ip) {
        std::string ret;
        ret.resize(af == AF_INET ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN);
        ex::wrapb(::inet_ntop(af, &ip, ret.data(), ret.size()), "inet_ntop()");
        return ret;
    }
    // Returns a non-blocking socket, ip may be nullptr for INADDR_ANY
    [[nodiscard]]
    inline fd bind_listen(const char* ip, uint16_t port) {
        c_api::fd fd {c_api::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        in_addr ia = ip ? c_api::inet_pton(AF_INET, ip) : in_addr{INADDR_ANY};
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
    inline fd accept(int fd) {
        c_api::fd client {::accept(fd, nullptr, nullptr)};
        if (client == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return c_api::fd{-1};
        }
        ex::wrape((int) client, "accept()");
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
