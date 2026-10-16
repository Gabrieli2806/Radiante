#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

// Work the game should not feel: compiles started while frames keep being drawn (ShaderPrewarm). A thread marked as
// background runs below normal priority, and the loops it starts (prism::parallelFor, the driver's deferred
// pipeline compiles) use at most half the cores, so the render thread and the chunk builders keep theirs. The threads
// those loops start are marked in turn, so the limit holds however deep the work goes.
namespace prism {

// Marks the calling thread as background work and lowers its priority. Idempotent.
void enterBackgroundThread();
bool isBackgroundThread();
// The most threads one background loop may use: half the hardware threads, at least one.
uint32_t backgroundThreadLimit();

// A thread with a stack of its own size. Shader includes are resolved recursively, which a default 1 MB stack can
// run out of (the Java side rebuilds on a 512 MB stack for the same reason). Joined by join(), never detached:
// the destructor joins too.
class LargeStackThread {
  public:
    LargeStackThread();
    LargeStackThread(size_t stackBytes, std::function<void()> body);
    ~LargeStackThread();
    LargeStackThread(const LargeStackThread &) = delete;
    LargeStackThread &operator=(const LargeStackThread &) = delete;
    LargeStackThread(LargeStackThread &&other) noexcept;
    LargeStackThread &operator=(LargeStackThread &&other) noexcept;

    bool joinable() const;
    void join();

  private:
    struct Handle;
    std::unique_ptr<Handle> handle_;
};

} // namespace prism
