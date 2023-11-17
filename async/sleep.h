#pragma once
#include "posix_wrappers.h"
#include "coro.h"
#include "poll_loop.h"

namespace async {
    inline task<void> sleep(double ms) {
        if (ms == 0) { co_return; }
        const long sec = ms / 1000;
        const long ns = (ms - sec * 1000) * 1e6;
        c_api::fd fd = c_api::timerfd_create();
        c_api::timerfd_settime(fd, 0, {{0, 0}, {sec, ns}});
        co_await poll_loop.wait_read(fd);
    }
}
