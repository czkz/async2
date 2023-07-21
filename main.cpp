#include <dbg.h>
#include <fmt.h>

#include "coro.h"
#include "poll_loop.h"

#include <ex.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <arpa/inet.h>

#include <thread>

#include "Buffer.h"



int get_connected_socket() {
    int sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(1337),
        .sin_addr = {inet_addr("10.0.0.254")},
        .sin_zero = {},
    };
    ex::wrape(connect(sfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), "connect()");
    return sfd;
}

namespace async::detail {
    class queue_buffer {
    public:
        void enqueue(std::string_view s) {
            raw_buffer.append(s);
        }
        size_t dequeue(size_t n, std::string& out) {
            std::string_view buf = buffer();
            out.append(buf.substr(0, n));
            start += n;
            if (start >= raw_buffer.size()) {
                raw_buffer.clear();
                start = 0;
            }
            return std::min(n, buf.size());
        }
        size_t dequeue_all(std::string& out) {
            std::string_view buf = buffer();
            out.append(buf);
            raw_buffer.clear();
            start = 0;
            return buf.size();
        }
        bool empty() const {
            return raw_buffer.empty();
        }
    private:
        std::string_view buffer() const {
            return std::string_view{raw_buffer}.substr(start);
        }
        std::string raw_buffer;
        size_t start = 0;
    };
    class fd_handle {
        int fd = -1;
    public:
        explicit fd_handle(int fd) noexcept : fd(fd) {}
        fd_handle(const fd_handle&) = delete;
        fd_handle(fd_handle&& o) noexcept { std::swap(fd, o.fd); }
        ~fd_handle() noexcept { if (fd != -1) { close(fd); } }
        operator int() const { return fd; }
    };
}

namespace async {
    class stream {
    public:
        task<size_t> read_some(std::string& out) {
            if (!buffer.empty()) {
                co_return buffer.dequeue_all(out);
            } else {
                co_await poll_loop.wait_read(fd);
                const size_t n_available = posix::c_api::available_bytes(fd);
                out.resize(out.size() + n_available);
                size_t n_read = posix::c_api::recv(fd, out.data() + out.size() - n_available, n_available);
                assert(n_read == n_available);
                co_return n_read;
            }
        }
        task<std::string> read_some() {
            std::string ret;
            co_await read_some(ret);
            co_return ret;
        }
        task<void> read_n(size_t n, std::string& out) {
            if (!buffer.empty()) {
                n -= buffer.dequeue(n, out);
            }
            out.resize(out.size() + n);
            auto* const end_ptr = out.data() + out.size();
            while (n > 0) {
                co_await poll_loop.wait_read(fd);
                n -= posix::c_api::read(fd, end_ptr - n, n);
            }
        }
        task<std::string> read_n(size_t n) {
            std::string ret;
            co_await read_n(n, ret);
            co_return ret;
        }
        task<void> read_until(std::string_view substr, std::string& out) {
            while (true) {
                const size_t n_read = co_await read_some(out);
                auto all = std::string_view(out);
                size_t to_search_len = n_read + substr.size() - 1;
                if (to_search_len > all.size()) { to_search_len = all.size(); }
                const size_t pos = all.find(substr, all.size() - to_search_len);
                if (pos != std::string::npos) {
                    size_t end_i = pos + substr.size();
                    buffer.enqueue(all.substr(end_i));
                    out.resize(pos + substr.size());
                    break;
                }
            }
        }
        task<std::string> read_until(std::string_view substr) {
            std::string ret;
            co_await read_until(substr, ret);
            co_return ret;
        }
        task<void> write(std::string_view str) {
            str = str.substr(posix::c_api::send(fd, str));
            while (!str.empty()) [[unlikely]] {
                co_await poll_loop.wait_write(fd);
                str = str.substr(posix::c_api::send(fd, str));
            }
        }
        static stream from_fd(int fd) { return stream{fd}; }
    private:
        explicit stream(int fd) : fd(fd) {}
    private:
        detail::fd_handle fd;
        detail::queue_buffer buffer;
    };

    task<stream> connect(const char* ip, uint16_t port) {
        int fd = posix::c_api::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        in_addr ia;
        if (inet_aton(ip, &ia) == 0) {
            throw ex::runtime("invalid ip address");
        }
        sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(port),
            .sin_addr = ia,
            .sin_zero = {},
        };
        int res = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (res == -1) {
            if (errno != EINPROGRESS) {
                throw ex::fn("connect()", strerror(errno));
            }
            co_await poll_loop.wait_write(fd);
            int err = posix::c_api::getsockopt(fd, SOL_SOCKET, SO_ERROR);
            if (err != 0) {
                throw ex::fn("connect()", strerror(err));
            }
        }
        co_return stream::from_fd(fd);
    }
};

task<void> foo() {
    prn("foo() start");
    auto stream = co_await async::connect("93.184.216.34", 80);
    prn("connected.");
    co_await stream.write("HEAD / HTTP/1.1\r\nHost:example.com\r\nConnection:close\r\n\r\n");
    auto buf = co_await stream.read_until("\r\n\r\n");
    prn(buf);
    prn("foo() end");
    co_return;
}

int main() {
    // std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto task = foo();
    rethrow_task(task);
    while (poll_loop.has_tasks()) {
        dp("MAIN LOOP");
        poll_loop.think();
        dp("MAIN LOOP END");
    }
    rethrow_task(task);
    dp("main end");
    task.handle.destroy();
}
