#pragma once
#include "stream.h"

namespace async {
    inline task<stream<provider::fd>> open_read(std::string_view path) {
        co_return stream(provider::fd(c_api::open(path, O_RDONLY | O_NONBLOCK)));
    }

    inline task<stream<provider::fd>> open_write(std::string_view path, bool append, bool create = true) {
        int flags = O_WRONLY | O_NONBLOCK;
        if (append) { flags |= O_APPEND; }
        if (create) { flags |= O_CREAT | O_TRUNC; }
        co_return stream(provider::fd(c_api::open(path, flags)));
    }

    inline task<stream<provider::fd_pair>> open_rw(std::string_view read_path, std::string_view write_path, bool append, bool create = true) {
        int flags = O_WRONLY | O_NONBLOCK;
        if (append) { flags |= O_APPEND; }
        if (create) { flags |= O_CREAT | O_TRUNC; }
        c_api::fd read_fd = c_api::open(read_path, O_RDONLY | O_NONBLOCK);
        c_api::fd write_fd = c_api::open(write_path, flags);
        co_return stream(provider::fd_pair(std::move(read_fd), std::move(write_fd)));
    }

    inline task<std::string> slurp(std::string_view path) {
        stream stream = co_await open_read(path);
        co_return co_await stream.read_until_eof();
    }
}
