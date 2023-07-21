#pragma once
#include <coroutine>
#include <variant>
#include <exception>

class suspend_when {
    bool suspend;
public:
    constexpr explicit suspend_when(bool b) : suspend(b) {}
    constexpr bool await_ready() const noexcept { return !suspend; }
    constexpr void await_suspend(std::coroutine_handle<>) const noexcept {}
    constexpr void await_resume() const noexcept {}
};

template <typename T>
class task {
public:
    struct promise_type;
    struct awaiter;
    awaiter operator co_await() { was_awaited = true; return awaiter{handle}; }
    // ~task() { if (!was_awaited) { this->handle.destroy(); } }
// private:
    friend void rethrow_task(task<void>);
    explicit task(std::coroutine_handle<promise_type> h) : handle(h) {}
    std::coroutine_handle<promise_type> handle;
    bool was_awaited = false;
};

struct promise_base {
    std::suspend_never initial_suspend() noexcept { return {}; }
    suspend_when final_suspend() noexcept {
        if (this->caller) { this->caller.resume(); }
        return suspend_when(!this->caller);
    }
    std::coroutine_handle<> caller;
};

template <typename T>
struct task<T>::promise_type : public promise_base {
    task get_return_object() { return task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
    void unhandled_exception() noexcept {
        this->result.template emplace<2>(std::current_exception());
    }
    template <std::convertible_to<T> From>
    void return_value(From&& v) {
        this->result.template emplace<1>(std::forward<From>(v));
    }
    std::variant<std::monostate, T, std::exception_ptr> result;
};

template <typename T>
struct task<T>::awaiter {
    bool await_ready() const noexcept {
        auto& promise = this->handle.promise();
        return promise.result.index() != 0;
    }
    void await_suspend(std::coroutine_handle<> h) {
        this->handle.promise().caller = h;
    }
    T await_resume() const {
        auto result = std::move(this->handle.promise().result);
        if (!this->handle.promise().caller) {
            this->handle.destroy();
        }
        if (result.index() == 2) {
            std::rethrow_exception(std::get<2>(std::move(result)));
        }
        return std::get<1>(std::move(result));
    }
    std::coroutine_handle<promise_type> handle;
};


template <>
struct task<void>::promise_type : public promise_base {
    task get_return_object() { return task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
    void unhandled_exception() noexcept {
        this->exception = std::current_exception();
    }
    constexpr void return_void() const noexcept {}
    std::exception_ptr exception;
};

template <>
struct task<void>::awaiter {
    bool await_ready() const noexcept {
        return this->handle.done();
    }
    void await_suspend(std::coroutine_handle<> h) {
        this->handle.promise().caller = h;
    }
    void await_resume() const {
        auto exception = std::move(this->handle.promise().exception);
        if (!this->handle.promise().caller) {
            this->handle.destroy();
        }
        if (exception) { std::rethrow_exception(exception); }
    }
    std::coroutine_handle<promise_type> handle;
};

inline void rethrow_task(task<void> t) {
    auto& exception = t.handle.promise().exception;
    if (exception) {
        std::rethrow_exception(exception);
    }
}
