#include "prism/util/background_work.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <pthread.h>
#    include <sys/resource.h>
#    include <sys/syscall.h>
#    include <unistd.h>
#endif

namespace prism {
namespace {

thread_local bool g_background = false;

void lowerCurrentThreadPriority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#else
    // On Linux a thread's nice value is its own (the thread id stands for the process here).
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}

} // namespace

void enterBackgroundThread() {
    if (g_background) return;
    g_background = true;
    lowerCurrentThreadPriority();
}

bool isBackgroundThread() {
    return g_background;
}

uint32_t backgroundThreadLimit() {
    return std::max(1u, std::thread::hardware_concurrency() / 2);
}

// ---- LargeStackThread

struct LargeStackThread::Handle {
    std::function<void()> body;
#ifdef _WIN32
    HANDLE thread = nullptr;
#else
    pthread_t thread{};
#endif
    bool joined = false;
};

namespace {

#ifdef _WIN32
DWORD WINAPI runThread(LPVOID parameter) {
    auto *body = static_cast<std::function<void()> *>(parameter);
    (*body)();
    return 0;
}
#else
void *runThread(void *parameter) {
    auto *body = static_cast<std::function<void()> *>(parameter);
    (*body)();
    return nullptr;
}
#endif

} // namespace

LargeStackThread::LargeStackThread() = default;

LargeStackThread::LargeStackThread(size_t stackBytes, std::function<void()> body)
    : handle_(std::make_unique<Handle>()) {
    handle_->body = std::move(body);
#ifdef _WIN32
    // Only reserved, not committed: the pages are taken as the stack grows.
    handle_->thread = CreateThread(nullptr, stackBytes, runThread, &handle_->body, STACK_SIZE_PARAM_IS_A_RESERVATION,
                                   nullptr);
    if (handle_->thread == nullptr) throw std::runtime_error("cannot start a thread");
#else
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, stackBytes);
    const int result = pthread_create(&handle_->thread, &attributes, runThread, &handle_->body);
    pthread_attr_destroy(&attributes);
    if (result != 0) throw std::runtime_error("cannot start a thread");
#endif
}

LargeStackThread::~LargeStackThread() {
    join();
}

LargeStackThread::LargeStackThread(LargeStackThread &&other) noexcept = default;

LargeStackThread &LargeStackThread::operator=(LargeStackThread &&other) noexcept {
    if (this != &other) {
        join();
        handle_ = std::move(other.handle_);
    }
    return *this;
}

bool LargeStackThread::joinable() const {
    return handle_ != nullptr && !handle_->joined;
}

void LargeStackThread::join() {
    if (!joinable()) return;
#ifdef _WIN32
    WaitForSingleObject(handle_->thread, INFINITE);
    CloseHandle(handle_->thread);
#else
    pthread_join(handle_->thread, nullptr);
#endif
    handle_->joined = true;
}

} // namespace prism
