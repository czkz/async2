#pragma once
#include "coro.h"
#include "poll_loop.h"
#include "posix_wrappers.h"

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

    inline task<c_api::fd> make_connected_socket(std::string_view ip, uint16_t port, int type, int protocol) {
        c_api::fd fd = c_api::socket(AF_INET, type, protocol);
        sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(port),
            .sin_addr = c_api::inet_pton(AF_INET, ip),
            .sin_zero = {},
        };
        int res = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (res == -1) {
            if (errno != EINPROGRESS) {
                throw ex::fn("connect()", strerror(errno));
            }
            co_await poll_loop.wait_write(fd);
            int err = c_api::getsockopt(fd, SOL_SOCKET, SO_ERROR);
            if (err != 0) {
                throw ex::fn("connect()", strerror(err));
            }
        }
        co_return fd;
    }
}

namespace async::provider {
    class fd {
    public:
        explicit fd(c_api::fd fd_handle) : fd_handle(std::move(fd_handle)) {}

        static constexpr bool stream_oriented = true;
        static constexpr bool has_lookahead = true;

    protected:
        task<void> wait_read() { co_await poll_loop.wait_read(fd_handle); }
        task<void> wait_write() { co_await poll_loop.wait_write(fd_handle); }

        size_t read(void* buf, size_t size) { return c_api::read(fd_handle, buf, size); }
        size_t write(std::string_view data) { return c_api::write(fd_handle, data); }

        task<void> close() { c_api::close(fd_handle); co_return; }

        // Available only if has_lookahead
        size_t available_bytes() { return c_api::available_bytes(fd_handle); }

    private:
        c_api::fd fd_handle;
    };

    class fd_pair {
    public:
        explicit fd_pair(c_api::fd read_fd, c_api::fd write_fd)
            : read_fd(std::move(read_fd))
            , write_fd(std::move(write_fd))
        {}

        static constexpr bool stream_oriented = true;
        static constexpr bool has_lookahead = true;

    protected:
        task<void> wait_read() { co_await poll_loop.wait_read(read_fd); }
        task<void> wait_write() { co_await poll_loop.wait_write(write_fd); }

        size_t read(void* buf, size_t size) { return c_api::read(read_fd, buf, size); }
        size_t write(std::string_view data) { return c_api::write(write_fd, data); }

        task<void> close() { c_api::close(read_fd); c_api::close(write_fd); co_return; }

        // Available only if has_lookahead
        size_t available_bytes() { return c_api::available_bytes(read_fd); }

    private:
        c_api::fd read_fd, write_fd;
    };
}

namespace async {
    template <typename Provider>
    class stream : private Provider {
        static_assert(Provider::stream_oriented == true);
    public:
        task<size_t> read_some(std::string& out) {
            if (!buffer.empty()) {
                co_return buffer.dequeue_all(out);
            } else {
                co_await Provider::wait_read();
                size_t buflen;
                if constexpr (Provider::has_lookahead) {
                    buflen = Provider::available_bytes();
                } else {
                    buflen = 1024;
                }
                out.resize(out.size() + buflen);
                size_t n_read = Provider::read(out.data() + out.size() - buflen, buflen);
                out.resize(out.size() - buflen + n_read);
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
                co_await Provider::wait_read();
                n -= Provider::read(end_ptr - n, n);
            }
        }
        task<std::string> read_n(size_t n) {
            std::string ret;
            co_await read_n(n, ret);
            co_return ret;
        }
        task<void> read_until_eof(std::string& out) {
            if (!buffer.empty()) {
                buffer.dequeue_all(out);
            }
            try {
                while (true) {
                    co_await read_some(out);
                }
            } catch (const c_api::eof&) {}
        }
        task<std::string> read_until_eof() {
            std::string ret;
            co_await read_until_eof(ret);
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
            str = str.substr(Provider::write(str));
            while (!str.empty()) {
                co_await Provider::wait_write();
                str = str.substr(Provider::write(str));
            }
        }
        using Provider::close;
        stream(Provider provider) : Provider(std::move(provider)) {}
    private:
        detail::queue_buffer buffer;
    };

    template <typename Provider>
    class msgstream : private Provider {
        static_assert(Provider::stream_oriented == false);
    public:
        task<std::string> read() {
            std::string ret;
            ret.resize(Provider::packet_size());
            co_await Provider::wait_read();
            size_t n_read = Provider::read(ret.data(), ret.size());
            ret.resize(n_read);
            co_return ret;
        }
        task<void> write(std::string_view str) {
            co_await Provider::wait_write();
            size_t n_sent = Provider::write(str);
            assert(n_sent == str.size());
        }
        using Provider::close;
        msgstream(Provider provider) : Provider(std::move(provider)) {}
    };
}
