#pragma once

#include "prism/all_extern.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace gpu {
// The queues are shared with Minecraft's Vulkan backend, which submits and presents from Java, and a queue may only be
// used by one thread at a time. Every queue operation on either side holds this mutex: Java takes it around its own
// (RendererProxy.lockQueue), the hooks below around the renderer's. It is recursive, so a thread that holds it can
// still call a hooked function.
std::recursive_mutex &queueMutex();

// For hang reports: the queue operation that holds queueMutex() (nullptr when none) and when it began, in
// std::chrono::steady_clock ticks.
std::atomic<const char *> &queueOp();
std::atomic<int64_t> &queueOpStart();

// Points volk's vkQueueSubmit, vkQueueSubmit2, vkQueueSubmit2KHR, vkQueueWaitIdle, vkDeviceWaitIdle and
// vkQueuePresentKHR at wrappers that hold queueMutex(). volkLoadDevice() replaces them, so this is called after it;
// calling it again changes nothing.
void installQueueLockHooks();
} // namespace gpu
