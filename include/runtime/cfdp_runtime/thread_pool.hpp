#pragma once

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "atomic_queue.hpp"

namespace
{
using ::cfdp::runtime::atomic::AtomicQueue;
} // namespace

namespace cfdp::runtime::thread_pool
{
template <class T>
class Future
{
  public:
    Future(std::future<T> future) : internal(std::move(future)) {}

    ~Future()               = default;
    Future(Future&& future) = default;

    Future(const Future&)            = delete;
    Future& operator=(Future const&) = delete;
    Future& operator=(Future&&)      = delete;

    T get() { return internal.get(); }

    [[nodiscard]] std::future_status poll() const noexcept
    {
        return internal.wait_for(std::chrono::seconds(0));
    };

    [[nodiscard]] bool isReady() const noexcept { return poll() == std::future_status::ready; }

  private:
    std::future<T> internal;
};

class ThreadPool
{
  public:
    ThreadPool(size_t numWorkers = std::thread::hardware_concurrency());
    ~ThreadPool() { shutdown(); };

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(ThreadPool const&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    template <class T, class... Args>
    Future<T> dispatchTask(std::function<T(Args...)> func, Args... args) noexcept;

    void shutdown() noexcept;

  private:
    std::atomic_bool shutdownFlag;
    std::vector<std::thread> workers;
    AtomicQueue<std::function<void()>> queue;
};
} // namespace cfdp::runtime::thread_pool

namespace
{
using ::cfdp::runtime::thread_pool::Future;
using ::cfdp::runtime::thread_pool::ThreadPool;
} // namespace

template <class T, class... Args>
Future<T> ThreadPool::dispatchTask(std::function<T(Args...)> func, Args... args) noexcept
{
    // NOTE: 06.10.2024 <@uncommon-nickname>
    // In a perfect world we would just move the promise into the lambda,
    // however we cannot assign the moving lambda to the std::function
    // handle. For now we can wrap the promise with `shared_ptr` and
    // copy it inside, dropping the original reference.
    auto promise = std::make_shared<std::promise<T>>();
    auto future  = promise->get_future();

    // NOTE: 06.10.2024 <@uncommon-nickname>
    // For the same reason as above, we cannot really use std::forward
    // to perform variadic move into the lambda. For now let's make
    // a copy, for memory safety only `shared_ptrs` should be passed
    // here anyway.
    std::function<void()> wrapper = [promise, func, args...]() mutable {
        try
        {
            auto result = func(args...);
            promise->set_value(result);
        }
        catch (...)
        {
            promise->set_exception(std::current_exception());
        }
    };

    queue.push(std::move(wrapper));

    return Future{std::move(future)};
};
