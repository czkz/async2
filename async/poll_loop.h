#pragma once
#include <coroutine>
#include <cstring>
#include <vector>
#include <string>
#include <stdexcept>
#include <poll.h>

struct poll_loop_t {
    struct awaiter;

    awaiter wait_events(int fd, short events);
    auto wait_read(int fd);
    auto wait_write(int fd);

    void think();
    bool has_tasks() const { return !suspended.empty(); }

private:
    void swap_remove(size_t i);
    std::vector<std::coroutine_handle<>> suspended;
    std::vector<pollfd> pfds;
};

inline thread_local poll_loop_t poll_loop;


struct poll_loop_t::awaiter {
    bool await_ready() {
        const int n_resumeable = poll(&pfd, 1, 0);
        if (n_resumeable == -1) {
            throw std::runtime_error(std::string("poll() failed: ") + strerror(errno));
        }
        return n_resumeable > 0;
    }
    void await_suspend(std::coroutine_handle<> h) {
        this->resumer->suspended.push_back(h);
        this->resumer->pfds.push_back(pfd);
    }
    void await_resume() const {}

    poll_loop_t* resumer;
    pollfd pfd;
};


inline poll_loop_t::awaiter poll_loop_t::wait_events(int fd, short events) {
    return {
        .resumer = this,
        .pfd = {
            .fd = fd,
            .events = events,
            .revents = 0,
        },
    };
}

inline auto poll_loop_t::wait_read(int fd) { return wait_events(fd, POLLIN); }
inline auto poll_loop_t::wait_write(int fd) { return wait_events(fd, POLLOUT); }

inline void poll_loop_t::think() {
    const int n_resumeable = poll(pfds.data(), pfds.size(), -1);
    if (n_resumeable == -1) {
        throw std::runtime_error(std::string("poll() failed: ") + strerror(errno));
    }
    if (n_resumeable > 0) {
        int n_left = n_resumeable;
        for (size_t i = 0; i < suspended.size(); i++) {
            if (pfds[i].revents != 0) {
                suspended[i].resume();
                swap_remove(i--);
                if (--n_left == 0) { break; }
            }
        }
    }
}

inline void poll_loop_t::swap_remove(size_t i) {
    std::swap(suspended[i], suspended.back());
    std::swap(pfds[i], pfds.back());
    suspended.pop_back();
    pfds.pop_back();
}

// TODO fix swap_remove(i--) underflow
