#pragma once
#include "coro.h"
#include "posix_wrappers.h"

namespace async {
    template <typename Transport>
    class stream {
    public:
        task<size_t> read_some(std::string& out) {
            if (!buffer.empty()) {
                co_return buffer.dequeue_all(out);
            } else {
                co_await transport.wait_read();
                size_t buflen;
                if constexpr (transport.has_lookahead) {
                    buflen = transport.available_bytes();
                } else {
                    buflen = 1024;
                }
                out.resize(out.size() + buflen);
                size_t n_read = transport.read(out.data() + out.size() - buflen, buflen);
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
                co_await transport.wait_read();
                n -= transport.read(end_ptr - n, n);
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
        task<void> write(std::string_view data) {
            co_await write_part(data);
            co_await flush();
        }
        task<void> write_part(std::string_view data) {
            data = data.substr(transport.write(data));
            while (!data.empty()) {
                co_await transport.wait_write();
                data = data.substr(transport.write(data));
            }
        }
        task<void> flush() { co_await transport.flush(); }
        task<void> close() { co_await transport.close(); }
        stream(Transport transport) : transport(std::move(transport)) {}

        Transport transport;
    private:
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
    private:
        queue_buffer buffer;
    };

    template <typename Transport>
    class msgstream {
    public:
        task<std::string> read() {
            std::string ret;
            co_await transport.wait_read();
            if constexpr (transport.has_lookahead) {
                ret.resize(transport.available_bytes());
            } else {
                ret.resize(transport.max_incoming_packet_size);
            }
            size_t n_read = transport.read(ret.data(), ret.size());
            ret.resize(n_read);
            co_return ret;
        }
        task<void> write(std::string_view data) {
            if (data.size() > transport.max_outgoing_packet_size) {
                throw ex::runtime("data size exceeds maximum packet size");
            }
            co_await transport.wait_write();
            size_t n_sent = transport.write(data);
            assert(n_sent == data.size());
        }
        task<void> close() { co_await transport.close(); }
        msgstream(Transport transport) : transport(std::move(transport)) {}

        Transport transport;
    };
}
