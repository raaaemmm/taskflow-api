#pragma once

// runs CPU-heavy work (Argon2) on a worker pool so event-loop threads never stall,
// then resumes the coroutine back on the original event loop.
#include <trantor/net/EventLoop.h>
#include <trantor/utils/ConcurrentTaskQueue.h>

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>

inline trantor::ConcurrentTaskQueue& cpuPool() {
    static trantor::ConcurrentTaskQueue pool(4, "cpu-pool");
    return pool;
}

template <typename F>
auto runBlocking(F fn) {
    using R = std::invoke_result_t<F>;
    struct Awaiter {
        F fn;
        trantor::EventLoop* loop{trantor::EventLoop::getEventLoopOfCurrentThread()};
        std::optional<R> result;
        std::exception_ptr err;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) {
            cpuPool().runTaskInQueue([this, h] {
                try {
                    result = fn();
                } catch (...) {
                    err = std::current_exception();
                }
                if (loop)
                    loop->queueInLoop([h] { h.resume(); });
                else
                    h.resume();
            });
        }
        R await_resume() {
            if (err) std::rethrow_exception(err);
            return std::move(*result);
        }
    };
    return Awaiter{std::move(fn)};
}
