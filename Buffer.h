#pragma once
#include <optional>
#include <string>
#include <stdexcept>
#include "posix_wrappers.h"

namespace async::detail {

    class ReceiverBuffer {
    protected:
        std::string buf;
        size_t filled = 0;
        size_t last_read = 0;
        std::exception_ptr eof;
        mutable bool throw_next_think = false;
        void rethrow_eof() const { if (eof) { throw_next_think = true; } }
    private:
        std::string consume_helper(std::string_view peeked) {
            if (!peeked.empty()) {
                if (peeked.size() == filled) {
                    std::string ret;
                    std::swap(buf, ret);
                    ret.resize(filled);
                    filled = 0;
                    last_read = 0;
                    return ret;
                } else {
                    std::string ret {peeked};
                    buf = buf.substr(peeked.size());
                    filled -= peeked.size();
                    last_read = 0;
                    return ret;
                }
            } else {
                return std::string();
            }
        }
    public:
        // Returns true if there is data to be consumed
        bool empty() const {
            return filled == 0;
        }

        // Peek data up to and including the target substring.
        // If target wasn't found, returns an empty string.
        std::string_view peek_with(std::string_view target, bool only_in_last_recv = false) const {
            size_t start;
            if (only_in_last_recv) {
                // Grab some old data in case target was split between reads
                size_t old_filled = filled - last_read;
                start = old_filled >= target.size() ? old_filled - (target.size() - 1) : 0;
            } else {
                start = 0;
            }
            std::string_view data = std::string_view(buf).substr(start, filled - start);
            const size_t pos = data.find(target);
            if (pos == std::string::npos) {
                rethrow_eof();
                return std::string_view {};
            } else {
                const std::string_view ret {data.substr(0, pos + target.size())};
                return ret;
            }
        }

        // Peek n bytes of data or nothing
        std::string_view peek_n(size_t n) {
            if (filled >= n) {
                return std::string_view(buf).substr(0, n);
            } else {
                rethrow_eof();
                return std::string_view();
            }
        }

        // Peek available data
        std::string_view peek_available() {
            if (empty()) { rethrow_eof(); }
            return std::string_view(buf).substr(0, filled);
        }

        // Consume data up to and including the target substring.
        // If target wasn't found, returns an empty string.
        std::string consume_with(std::string_view target, bool only_in_last_recv = false) {
            return consume_helper(peek_with(target, only_in_last_recv));
        }

        // Consume n bytes of data or nothing
        std::string consume_n(size_t n) {
            return consume_helper(peek_n(n));
        }

        // Peek available data
        std::string consume_available() {
            return consume_helper(peek_available());
        }

    protected:
        // Receive available data
        void update(int fd) {
            if (throw_next_think) { std::rethrow_exception(eof); }
            constexpr size_t buflen = 4096;
            last_read = 0;
            ssize_t n_read;
            do {
                buf.resize(filled + buflen);
                n_read = 0;
                if (!eof) {
                    try {
                        n_read = posix::c_api::recv(fd, buf.data() + filled, buflen);
                    } catch (const posix::c_api::eof& e) {
                        if (empty()) { throw; }
                        eof = std::current_exception();
                    }
                }
                filled += n_read;
                last_read += n_read;
            } while (n_read > 0);
        }
    };

    class SenderBuffer {
    protected:
        std::string buf;
        size_t total_sent = 0;
    public:
        std::string_view get_send_buffer() const { return buf; }

        // Queue data for sending
        void send(std::string data) {
            if (data.empty()) { return; }
            if (total_sent == buf.size()) { // Swap
                buf = std::move(data);
                total_sent = 0;
            } else if (total_sent == 0) { // Append
                buf += data;
            } else { // Shrink and append, unlikely
                buf = buf.substr(total_sent) + data;
            }
        }

    protected:
        // Send some data
        void update(int fd) {
            while (true) {
                std::string_view todo = std::string_view(buf).substr(total_sent);
                if (todo.empty()) { return; }
                ssize_t n_sent = posix::c_api::send(fd, todo);
                if (n_sent == 0) {
                    return;
                }
                total_sent += n_sent;
            }
        }
    };

}
