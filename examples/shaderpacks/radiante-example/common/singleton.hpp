#pragma once
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <utility>

// One process-wide instance of Derived, created explicitly with init() once the arguments it needs exist (the
// renderer needs the device handles Java hands over), rather than lazily on first use.
//
// Derived keeps its constructor private and befriends Singleton<Derived>, so init() is the only way to build one.
template <typename Derived>
class Singleton {
  public:
    // Builds the instance. A second call is a programming error and throws; a constructor that throws leaves nothing
    // behind, so init() can be tried again.
    template <typename... Args>
    static void init(Args &&...args) {
        std::lock_guard<std::mutex> lock(initMutex_);
        if (instance_.load(std::memory_order_relaxed) != nullptr) {
            throw std::logic_error("Singleton<>::init(): already initialized");
        }
        // Never deleted: static destructors run at process exit, possibly after the Vulkan driver has been unloaded
        // (always on Linux), and tearing the renderer down at that point crashes the game while it quits.
        Derived *created = new Derived(std::forward<Args>(args)...);
        instance_.store(created, std::memory_order_release);
    }

    // The instance; throws if init() has not run yet.
    static Derived &instance() {
        Derived *p = instance_.load(std::memory_order_acquire);
        if (p == nullptr) { throw std::logic_error("Singleton<>::instance(): not initialized yet"); }
        return *p;
    }

    static bool is_initialized() noexcept { return instance_.load(std::memory_order_acquire) != nullptr; }

    // The instance, or nullptr before init(): for callers that may run before the renderer exists.
    static Derived *try_instance() noexcept { return instance_.load(std::memory_order_acquire); }

  protected:
    Singleton() = default;
    ~Singleton() = default;
    Singleton(const Singleton &) = delete;
    Singleton &operator=(const Singleton &) = delete;
    Singleton(Singleton &&) = delete;
    Singleton &operator=(Singleton &&) = delete;

  private:
    // Readers only ever load the atomic; the mutex just keeps two racing init() calls from both constructing.
    static inline std::atomic<Derived *> instance_{nullptr};
    static inline std::mutex initMutex_;
};
