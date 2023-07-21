#pragma once
#include <stdexcept>
#include <fmt.h>
#include <string>
#include <cstring>
#include <cerrno>

#define exwrap(expr) (ex::wrap((expr), #expr))
#define exwrapb(expr) (ex::wrapb((expr), #expr))
#define exwrape(expr) (ex::wrape((expr), #expr))

namespace ex {
    class oom : public std::exception {
        char msg_buf[48] = {0};
    public:
        explicit oom(size_t size) {
            snprintf(msg_buf, sizeof(msg_buf), "Couldn't allocate %zu bytes", size);
        }
        const char* what() const noexcept override { return msg_buf; }
    };

    class runtime : public std::exception {
        std::string msg;
    public:
        explicit runtime(std::string msg)
            : msg(std::move(msg)) {}
        explicit runtime(const char* msg)
            : msg(msg) {}
        explicit runtime(const char* msg, int code)
            : msg(fmt_raw(msg, '(', code, ')')) {}
        virtual const char* what() const noexcept override { return msg.data(); }
    };

    class fn : public runtime {
    public:
        explicit fn(const char* fn_name)
            : runtime(fmt(fn_name, "failed")) {}
        explicit fn(const char* fn_name, int code)
            : runtime(fmt(fn_name, "failed with code", code)) {}
        explicit fn(const char* fn_name, const char* err)
            : runtime(fmt(fn_name, "failed:", err)) {}
    };

    inline void wrap(int res, const char* fn_name) { if (res) { throw fn(fn_name, res); } }
    inline void wrapb(bool res, const char* fn_name) { if (!res) { throw fn(fn_name); } }
    template <typename T>
    inline T wrape(T res, const char* fn_name) { if (res == -1) { throw fn(fn_name, strerror(errno)); } return res; }
}
