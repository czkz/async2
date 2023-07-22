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

#include "posix_wrappers.h"

#include <signal.h>

#include <unordered_map>


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
}

namespace async {
    namespace provider {
        class fd {
        public:
            explicit fd(c_api::fd fd_handle) : fd_handle(std::move(fd_handle)) {}
        protected:
            task<void> wait_read() { co_await poll_loop.wait_read(fd_handle); }
            task<void> wait_write() { co_await poll_loop.wait_write(fd_handle); }

            size_t read(void* buf, size_t size) { return c_api::read(fd_handle, buf, size); }
            size_t write(std::string_view data) { return c_api::write(fd_handle, data); }

            static constexpr bool has_lookahead = true;
            size_t available_bytes() { return c_api::available_bytes(fd_handle); }

            task<void> close() { c_api::close(fd_handle); co_return; }

        private:
            c_api::fd fd_handle;
        };

        class fd_pair {
        public:
            explicit fd_pair(c_api::fd read_fd, c_api::fd write_fd)
                : read_fd(std::move(read_fd))
                , write_fd(std::move(write_fd))
            {}
        protected:
            task<void> wait_read() { co_await poll_loop.wait_read(read_fd); }
            task<void> wait_write() { co_await poll_loop.wait_write(write_fd); }

            size_t read(void* buf, size_t size) { return c_api::read(read_fd, buf, size); }
            size_t write(std::string_view data) { return c_api::write(write_fd, data); }

            static constexpr bool has_lookahead = true;
            size_t available_bytes() { return c_api::available_bytes(read_fd); }

            task<void> close() { c_api::close(read_fd); c_api::close(write_fd); co_return; }

        private:
            c_api::fd read_fd, write_fd;
        };
    }
}

namespace async {

    template <typename Provider>
    class stream : private Provider {
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

    task<stream<provider::fd>> connect(const char* ip, uint16_t port) {
        c_api::fd fd = c_api::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        in_addr ia = c_api::inet_pton(AF_INET, ip);
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
            int err = c_api::getsockopt(fd, SOL_SOCKET, SO_ERROR);
            if (err != 0) {
                throw ex::fn("connect()", strerror(err));
            }
        }
        co_return stream(provider::fd(std::move(fd)));
    }

    task<stream<provider::fd>> open_read(const char* path) {
        co_return stream(provider::fd(c_api::open(path, O_RDONLY | O_NONBLOCK)));
    }

    task<stream<provider::fd>> open_write(const char* path, bool append, bool create = true) {
        int flags = O_WRONLY | O_NONBLOCK;
        if (append) { flags |= O_APPEND; }
        if (create) { flags |= O_CREAT | O_TRUNC; }
        co_return stream(provider::fd(c_api::open(path, flags)));
    }

    task<stream<provider::fd_pair>> open_rw(const char* read_path, const char* write_path, bool append, bool create = true) {
        int flags = O_WRONLY | O_NONBLOCK;
        if (append) { flags |= O_APPEND; }
        if (create) { flags |= O_CREAT | O_TRUNC; }
        c_api::fd read_fd = c_api::open(read_path, O_RDONLY | O_NONBLOCK);
        c_api::fd write_fd = c_api::open(write_path, flags);
        co_return stream(provider::fd_pair(std::move(read_fd), std::move(write_fd)));
    }

    task<std::string> slurp(const char* path) {
        stream stream = co_await open_read(path);
        co_return co_await stream.read_until_eof();
    }

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

    task<server> listen(const char* ip, uint16_t port) {
        co_return server::from_fd(c_api::bind_listen(ip, port));
    }
}

task<void> test_client() {
    async::stream stream = co_await async::connect("93.184.216.34", 80);
    prn("client connected.");
    co_await stream.write("HEAD / HTTP/1.1\r\nHost:example.com\r\nConnection:close\r\n\r\n");
    auto buf = co_await stream.read_until("\r\n\r\n");
    prn("client:", buf);
    co_return;
}

task<void> test_file_read() {
    prn(co_await async::slurp("/etc/hosts"));
}

task<void> test_file_write() {
    async::stream stream = co_await async::open_write("foo", false);
    co_await stream.write("bar\n");
    co_await stream.close();
    assert(co_await async::slurp("foo") == "bar\n");
}

task<void> test_file_rw() {
    async::stream stream = co_await async::open_rw("foo", "bar", false);
    co_await stream.write("baz\n");
    assert(co_await stream.read_until_eof() == "bar\n");
    co_await stream.close();
    assert(co_await async::slurp("bar") == "baz\n");
}

namespace async {
    task<std::unordered_map<size_t, in_addr>> parse_hosts() {
        async::stream stream = co_await async::open_read("/etc/hosts");
        std::unordered_map<size_t, in_addr> host_to_ip;
        try {
            std::vector<std::string_view> words;
            words.reserve(4);
            while (true) {
                std::string line = co_await stream.read_until("\n");
                static constexpr auto isws = [](char c) { return c == ' ' || c == '\t'; };
                static constexpr auto iseol = [](char c) { return c == '#' || c == '\n'; };
                static constexpr auto isw = [](char c) { return !isws(c) && !iseol(c); };
                size_t i = 0;
                while (true) {
                    // Line ends with \n, which is never stepped over
                    // in loops, so it's not nessesary to check that i < line.size()
                    while (isws(line[i])) { i++; }
                    if (iseol(line[i])) { break; }
                    size_t start_i = i;
                    while (isw(line[i])) { i++; }
                    words.push_back(std::string_view(line).substr(start_i, i - start_i));
                }
                if (words.size() > 1) {
                    line[words[0].size()] = '\0';
                    assert(words[0].size() == std::string_view(words[0].data()).size());
                    try {
                        in_addr ip = c_api::inet_pton(AF_INET, words[0].data());
                        for (size_t i = 1; i < words.size(); i++) {
                            host_to_ip.emplace(std::hash<std::string_view>{}(words[i]), ip);
                        }
                    } catch (const std::exception&) {}
                }
                words.clear();
            }
        } catch (const async::c_api::eof&) {}
        co_return host_to_ip;
    }
}

task<void> coro_main() {
    // co_await test_client();
    // co_await test_file_read();
    // co_await test_file_write();
    // co_await test_file_rw();
    co_await async::parse_hosts();
    co_return;
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    // std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto task = coro_main();
    while (poll_loop.has_tasks()) {
        dp("MAIN LOOP");
        poll_loop.think();
        dp("MAIN LOOP END");
    }
    rethrow_task(task);
    dp("main end");
    task.handle.destroy();
}
