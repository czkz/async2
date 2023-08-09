#pragma once
#include "stream.h"
#include "poll_loop.h"

namespace async::transport {
    class file {
    public:
        explicit file(c_api::fd fd_handle) : fd_handle(std::move(fd_handle)) {}

        task<void> wait_read() { co_await poll_loop.wait_read(fd_handle); }
        task<void> wait_write() { co_await poll_loop.wait_write(fd_handle); }
        size_t read(void* buf, size_t size) { return c_api::read(fd_handle, buf, size); }
        size_t write(std::string_view data) { return c_api::write(fd_handle, data); }

        task<void> flush() { co_return; }
        task<void> close() { c_api::close(fd_handle); co_return; }

        static constexpr bool has_lookahead = true;
        size_t available_bytes() { return c_api::available_bytes(fd_handle); }

        c_api::fd fd_handle;
    };

    class file_pair {
    public:
        explicit file_pair(c_api::fd read_fd, c_api::fd write_fd)
            : read_fd(std::move(read_fd))
            , write_fd(std::move(write_fd))
        {}

        task<void> wait_read() { co_await poll_loop.wait_read(read_fd); }
        task<void> wait_write() { co_await poll_loop.wait_write(write_fd); }
        size_t read(void* buf, size_t size) { return c_api::read(read_fd, buf, size); }
        size_t write(std::string_view data) { return c_api::write(write_fd, data); }

        task<void> flush() { co_return; }
        task<void> close() { c_api::close(read_fd); c_api::close(write_fd); co_return; }

        static constexpr bool has_lookahead = true;
        size_t available_bytes() { return c_api::available_bytes(read_fd); }

        c_api::fd read_fd, write_fd;
    };
}

namespace async::file {
    inline task<transport::file> open_read(std::string_view path) {
        co_return transport::file(c_api::open(path, O_RDONLY | O_NONBLOCK));
    }

    inline task<transport::file> open_write(std::string_view path, bool append, bool create = true) {
        int flags = O_WRONLY;
        if (append) { flags |= O_APPEND; }
        if (create) { flags |= O_CREAT | O_TRUNC; }
        co_return transport::file(c_api::open(path, flags));
    }

    inline task<transport::file_pair> open_rw(std::string_view read_path, std::string_view write_path, bool append, bool create = true) {
        int flags = O_WRONLY;
        if (append) { flags |= O_APPEND; }
        if (create) { flags |= O_CREAT | O_TRUNC; }
        c_api::fd read_fd = c_api::open(read_path, O_RDONLY);
        c_api::fd write_fd = c_api::open(write_path, flags);
        co_return transport::file_pair(std::move(read_fd), std::move(write_fd));
    }
}
