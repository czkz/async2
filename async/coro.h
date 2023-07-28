#pragma once
#include <cassert>
#include <coroutine>
#include <tuple>
#include <variant>
#include <exception>
#include <stdexcept>

namespace async::detail {
    class suspend_when {
        bool suspend;
    public:
        constexpr explicit suspend_when(bool b) : suspend(b) {}
        constexpr bool await_ready() const noexcept { return !suspend; }
        constexpr void await_suspend(std::coroutine_handle<>) const noexcept {}
        constexpr void await_resume() const noexcept {}
    };

    struct promise_base {
        std::suspend_never initial_suspend() noexcept { return {}; }
        detail::suspend_when final_suspend() noexcept {
            if (this->caller) { this->caller.resume(); }
            return detail::suspend_when(!this->caller);
        }
        std::coroutine_handle<> caller;
    };
}

namespace async {
    template <typename T>
    class task {
    public:
        struct promise_type;
        struct awaiter;
        awaiter operator co_await() {
            assert(!was_awaited);
            was_awaited = true;
            return awaiter{handle};
        }

        task(const task&) = delete;
        task(task&& o) {
            std::swap(handle, o.handle);
            std::swap(was_awaited, o.was_awaited);
        }
        ~task() {
            if (handle && !was_awaited) {
                try {
                    throw std::runtime_error("task was not awaited");
                } catch (const std::exception&) { std::terminate(); }
            }
        }
    // private:
        friend void rethrow_task(const task<void>&);
        explicit task(std::coroutine_handle<promise_type> h) : handle(h) {}
        std::coroutine_handle<promise_type> handle;
        bool was_awaited = false;
    };

    template <typename T>
    struct task<T>::promise_type : public detail::promise_base {
        using value_type = T;
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
    struct task<void>::promise_type : public detail::promise_base {
        using value_type = void;
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

    inline void rethrow_task(const task<void>& t) {
        auto& exception = t.handle.promise().exception;
        if (exception) {
            std::rethrow_exception(exception);
        }
    }

    template <typename Promise = void>
    struct this_task_handle {
        bool await_ready() { return false; }
        bool await_suspend(std::coroutine_handle<Promise> h) { ret = h; return false; }
        std::coroutine_handle<Promise> await_resume() { return ret; }
        std::coroutine_handle<Promise> ret;
    };

    template <typename... Tasks>
    auto gather(Tasks... tasks) -> task<std::tuple<typename Tasks::promise_type::value_type...>> {
        co_return std::make_tuple(co_await tasks...);
    }

    template <typename... Tasks>
    task<void> gather_void(Tasks... tasks) {
        (void) ((co_await tasks), ...);
    }
}
