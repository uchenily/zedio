#pragma once

#include "zedio/common/error.hpp"
#include "zedio/common/macros.hpp"
#include "zedio/common/util/noncopyable.hpp"
#include "zedio/io/base/callback.hpp"
#include "zedio/io/base/driver.hpp"

namespace zedio::io::detail {

template <class IO>
class IORegistrator {
public:
    template <typename F, typename... Args>
        requires std::is_invocable_v<F, io_uring_sqe *, Args...>
    IORegistrator(F &&f, Args... args)
        : sqe_{t_ring->get_sqe()} {
        if (sqe_ != nullptr) [[likely]] {
            std::invoke(std::forward<F>(f), sqe_, std::forward<Args>(args)...);
            io_uring_sqe_set_data(sqe_, &this->cb_);
        } else {
            t_driver->push_waiting_coro([this,
                                         f = std::forward<F>(f),
                                         ... args = std::forward<Args>(args)](io_uring_sqe *sqe) {
                std::invoke(f, sqe, args...);
                io_uring_sqe_set_data(sqe, &this->cb_);
            });
        }
    }

    // Delete copy
    IORegistrator(const IORegistrator &other) = delete;
    auto operator=(const IORegistrator &other) -> IORegistrator & = delete;
    // Delete move
    IORegistrator(IORegistrator &&other) = delete;
    auto operator=(IORegistrator &&other) -> IORegistrator & = delete;

    auto await_ready() const noexcept -> bool {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) {
        cb_.handle_ = std::move(handle);
        if (cb_.has_timeout_ != 0) {
            cb_.iter_ = time::detail::t_timer->add_timer_event(&cb_, cb_.deadline_);
        }
        if (sqe_ != nullptr) [[likely]] {
            t_ring->submit();
        }
    }

    [[REMEMBER_CO_AWAIT]]
    auto set_timeout(std::chrono::milliseconds timeout) -> IO & {
        return set_timeout_at(std::chrono::steady_clock::now() + timeout);
    }

    [[REMEMBER_CO_AWAIT]]
    auto set_timeout_at(std::chrono::steady_clock::time_point deadline) -> IO & {
        cb_.deadline_ = deadline;
        return *static_cast<IO *>(this);
    }

protected:
    Callback      cb_{};
    io_uring_sqe *sqe_;
};

} // namespace zedio::io::detail