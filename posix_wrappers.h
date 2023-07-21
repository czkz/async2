#pragma once
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
    inline size_t write(int sfd, std::string_view data) {
        ssize_t n_sent = ::write(sfd, data.data(), data.size());
        if (n_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        ex::wrape(n_sent, "write()");
        return n_sent;
    }
    // Returns number of bytes sent (may be zero)
    inline size_t send(int sfd, std::string_view data) {
        ssize_t n_sent = ::send(sfd, data.data(), data.size(), MSG_NOSIGNAL);
        if (n_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        ex::wrape(n_sent, "send()");
        return n_sent;
    }
    // Returns number of bytes read (may be zero)
    inline size_t read(int sfd, void* buf, size_t size) {
        ssize_t n_read = ::read(sfd, buf, size);
        if (n_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        } else if (n_read == 0) {
            throw eof();
        }
        ex::wrape(n_read, "read()");
        return n_read;
    }
    // Returns number of bytes received (may be zero)
    inline size_t recv(int sfd, void* buf, size_t size) {
        ssize_t n_read = ::recv(sfd, buf, size, MSG_NOSIGNAL);
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
    inline size_t available_bytes(int fd) {
        int value;
        ex::wrape(::ioctl(fd, FIONREAD, &value), "ioctl()");
        return value;
    }
    inline void setsockopt(int fd, int level, int optname, int optval) {
        ex::wrape(::setsockopt(fd, level, optname, &optval, sizeof(optval)), "setsockopt()");
    }
    inline int getsockopt(int fd, int level, int optname) {
        int optval;
        socklen_t optlen = sizeof(optval);
        ex::wrape(::getsockopt(fd, level, optname, &optval, &optlen), "getsockopt()");
        assert(optlen == sizeof(optval));
        return optval;
    }
    // Returns a valid non-blocking socket
    inline int socket(int domain, int type, int protocol) {
        int fd = ex::wrape(::socket(domain, type, protocol), "socket()");
        c_api::fcntl(fd, F_SETFL, O_NONBLOCK);
        return fd;
    }
    // Always returns a valid socket
    inline int bind_listen(const char* host, const char* port) {
        addrinfo hints {};
        hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* ai_list;
        int res = getaddrinfo(host, port, &hints, &ai_list);
        if (res != 0) {
            throw ex::runtime(fmt("getaddrinfo:", gai_strerror(res)));
        }

        // getaddrinfo() returns a list of address structures.
        // Try each address until we successfully bind(2).
        // If socket(2) or bind(2) fails, we close the socket
        // and try the next address.

        int sfd;
        addrinfo* ai;
        int saved_err = 0;
        for (ai = ai_list; ai != NULL; ai = ai->ai_next) {
            sfd = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK, ai->ai_protocol);
            if (sfd == -1) {
                continue;
            }
            setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, 1);
            if (bind(sfd, ai->ai_addr, ai->ai_addrlen) != -1) {
                break;  // Success
            }
            saved_err = errno;
            close(sfd);
        }

        freeaddrinfo(ai_list);

        if (ai == NULL) {
            throw ex::fn("bind()", strerror(saved_err));
        }

        ex::wrape(listen(sfd, 256), "listen()");

        return sfd;
    }
    // Returns accepted socket or -1
    inline int accept(int sfd) {
        int client = ::accept(sfd, nullptr, nullptr);
        if (client == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return -1;
        }
        ex::wrape(client, "accept()");
        fcntl(client, F_SETFL, O_NONBLOCK);
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
