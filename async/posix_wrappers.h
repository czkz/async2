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
    inline fd open(std::string_view pathname, int flags, mode_t mode = 00666) {
        c_api::fd fd {ex::wrape(::open(std::string(pathname).c_str(), flags, mode), "open()")};
        c_api::fcntl(fd, F_SETFL, O_NONBLOCK);
        return fd;
    }
    // Returns a non-blocking fd
    [[nodiscard]]
    inline fd creat(std::string_view pathname, mode_t mode = 00666) {
        c_api::fd fd {ex::wrape(::creat(std::string(pathname).c_str(), mode), "creat()")};
        c_api::fcntl(fd, F_SETFL, O_NONBLOCK);
        return fd;
    }
    // Optional, use to close a descriptor early
    inline void close(fd& fd) noexcept {
        ::close(fd.release());
    }
    [[nodiscard]]
    inline in_addr inet_pton(int af, std::string_view ip) {
        in_addr ret;
        int res = ::inet_pton(af, std::string(ip).c_str(), &ret);
        if (res == 0) {
            throw ex::fn("inet_pton()");
        } else if (res == -1) {
            throw ex::fn("inet_pton()", strerror(errno));
        }
        return ret;
    }
    [[nodiscard]]
    inline uint32_t inet_ptoh(int af, std::string_view ip) {
        return ntohl(c_api::inet_pton(af, ip).s_addr);
    }
    [[nodiscard]]
    inline std::string inet_ntop(int af, in_addr ip) {
        std::string ret;
        ret.resize(af == AF_INET ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN);
        bool res = ::inet_ntop(af, &ip, ret.data(), ret.size());
        if (res == 0) {
            throw ex::fn("inet_ntop()", strerror(errno));
        }
        // Trim to null byte
        return std::string(ret.c_str());
    }
    [[nodiscard]]
    inline std::string inet_htop(int af, uint32_t ip) {
        return c_api::inet_ntop(af, in_addr{htonl(ip)});
    }
    // Returns a non-blocking socket, ip may be nullptr for INADDR_ANY
    [[nodiscard]]
    inline fd bind_listen(std::string_view ip, uint16_t port) {
        c_api::fd fd {c_api::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        in_addr ia = (ip == "0.0.0.0") ? c_api::inet_pton(AF_INET, ip) : in_addr{INADDR_ANY};
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
