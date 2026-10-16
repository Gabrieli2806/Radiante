#pragma once

#include "prism/util/background_work.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

namespace prism {

// The environment variable that sets how many threads a shader pack build uses, and the default of the loops below.
inline constexpr const char *kBuildThreadsVariable = "PRISM_SHADER_PACK_BUILD_THREADS";

// Threads for a parallel loop: the number in the environment variable `envName` when it holds a positive one (but no
// more than the hardware runs at once), otherwise as many as the hardware runs, up to 8.
inline uint32_t parallelThreadCount(const char *envName = kBuildThreadsVariable) {
    const uint32_t hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    const char *value = envName != nullptr ? std::getenv(envName) : nullptr;
    if (value != nullptr && value[0] != '\0') {
        char *end = nullptr;
        const long requested = std::strtol(value, &end, 10);
        if (end != value && requested > 0) { return static_cast<uint32_t>(std::min<long>(requested, hardwareThreads)); }
    }
    return std::min(8u, hardwareThreads);
}

namespace detail {

// Calls body(k) for every k below `count`: on `threads` new threads when that is more than one, each taking the next
// k as it finishes one, otherwise in order on the calling thread. A call that throws does not stop the others; the
// first exception is rethrown once every call has returned.
template <typename Body>
void runEach(size_t count, uint32_t threads, Body &body) {
    std::atomic<size_t> next{0};
    std::exception_ptr failure;
    std::mutex failureMutex;
    const auto work = [&] {
        for (size_t k = next.fetch_add(1, std::memory_order_relaxed); k < count;
             k = next.fetch_add(1, std::memory_order_relaxed)) {
            try {
                body(k);
            } catch (...) {
                std::scoped_lock lock(failureMutex);
                if (failure == nullptr) { failure = std::current_exception(); }
            }
        }
    };
    if (threads > 1) {
        // Helpers of background work are background work too (see background_work.hpp).
        const bool background = isBackgroundThread();
        const auto helper = [&work, background] {
            if (background) { enterBackgroundThread(); }
            work();
        };
        std::vector<std::jthread> workers;
        workers.reserve(threads);
        for (uint32_t i = 0; i < threads; i++) {
            try {
                workers.emplace_back(helper);
            } catch (const std::system_error &) {
                // The system is out of threads: those already running take the remaining items.
                break;
            }
        }
        if (workers.empty()) { work(); }
    } else {
        work();
    }
    if (failure != nullptr) { std::rethrow_exception(failure); }
}

} // namespace detail

// Calls fn(i) for every i below `count`, spread over parallelThreadCount(envName) threads (on the calling thread when
// that is one; on a background thread, no more than backgroundThreadLimit()). Every call is made even when some
// throw; the first exception is then rethrown.
template <typename Fn>
void parallelFor(size_t count, Fn &&fn, const char *envName = kBuildThreadsVariable) {
    if (count == 0) { return; }
    uint32_t wanted = parallelThreadCount(envName);
    if (isBackgroundThread()) { wanted = std::min(wanted, backgroundThreadLimit()); }
    const auto threads = static_cast<uint32_t>(std::min<size_t>(wanted, count));
    detail::runEach(count, threads, fn);
}

} // namespace prism
