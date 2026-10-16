#include "prism/gpu/queue_lock.hpp"

#include <chrono>
#include <type_traits>

namespace gpu {
namespace {

// Names the queue operation for as long as it runs, then gives the name back to the one it ran inside of (a Java-side
// lock, or another hooked call on the same thread).
class QueueOpScope {
  public:
    explicit QueueOpScope(const char *op) : outer_(queueOp().load()) {
        queueOp().store(op);
        queueOpStart().store(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    ~QueueOpScope() {
        queueOp().store(outer_);
    }

    QueueOpScope(const QueueOpScope &) = delete;
    QueueOpScope &operator=(const QueueOpScope &) = delete;

  private:
    const char *outer_;
};

// The hook of one volk function pointer (Slot): the driver function it replaced, and the wrapper put in its place.
template <auto *Slot, typename Function = std::remove_pointer_t<decltype(Slot)>>
struct QueueHook;

template <auto *Slot, typename Result, typename... Args>
struct QueueHook<Slot, Result(VKAPI_PTR *)(Args...)> {
    static inline Result(VKAPI_PTR *driver)(Args...) = nullptr;
    static inline const char *name = nullptr;

    static VKAPI_ATTR Result VKAPI_CALL call(Args... args) {
        std::scoped_lock lock(queueMutex());
        QueueOpScope scope(name);
        return driver(args...);
    }

    static void install(const char *opName) {
        auto &slot = *Slot;
        // Not loaded (the device lacks the extension), or hooked already.
        if (slot == nullptr || slot == &call) return;
        name = opName;
        driver = slot;
        slot = &call;
    }
};

} // namespace

std::recursive_mutex &queueMutex() {
    // Never destroyed: threads of the renderer and of Minecraft may still take it while the process exits.
    static auto *mutex = new std::recursive_mutex();
    return *mutex;
}

std::atomic<const char *> &queueOp() {
    static std::atomic<const char *> op{nullptr};
    return op;
}

std::atomic<int64_t> &queueOpStart() {
    static std::atomic<int64_t> start{0};
    return start;
}

void installQueueLockHooks() {
    QueueHook<&vkQueueSubmit>::install("native vkQueueSubmit");
    QueueHook<&vkQueueSubmit2>::install("native vkQueueSubmit2");
    QueueHook<&vkQueueSubmit2KHR>::install("native vkQueueSubmit2KHR");
    QueueHook<&vkQueueWaitIdle>::install("native vkQueueWaitIdle");
    QueueHook<&vkDeviceWaitIdle>::install("native vkDeviceWaitIdle");
    QueueHook<&vkQueuePresentKHR>::install("native vkQueuePresentKHR");
}

} // namespace gpu
