#pragma once

#include "prism/all_extern.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

class Framework;

namespace framegen {

// The blit of a frame into a swapchain image, clipped to the window. Minecraft keeps its images bottom-up and flips
// them on the way to the window; flipY does the same.
VkImageBlit swapchainBlit(
    uint32_t width, uint32_t height, uint32_t swapchainWidth, uint32_t swapchainHeight, bool flipY);

// Shows frames on further images of Minecraft's swapchain, from a thread of its own: after Minecraft has presented a
// frame, the rest of that frame's set (the other generated frames, then the real one) is presented spread over the
// time one rendered frame takes, so the pacing does not depend on the present mode.
//
// The render thread prepares and posts; the thread then owns the job and the present objects until it is idle again.
class PresentThread {
  public:
    // What one rendered frame leaves to show.
    struct Job {
        std::vector<VkImage> images; // shown in this order, each in GENERAL
        uint32_t width = 0;
        uint32_t height = 0;
        bool flipY = true;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> swapchainImages;
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t swapchainWidth = 0;
        uint32_t swapchainHeight = 0;
        // Image k is shown at start + spacing * (k + 1).
        std::chrono::steady_clock::time_point start{};
        std::chrono::duration<double> spacing{0.0};
    };

    // How the last job went: 0 shown, 1 the swapchain is suboptimal, -1 it is out of date (or a present failed).
    struct Outcome {
        int status = 0;
        uint32_t presented = 0;
    };

    PresentThread() = default;
    ~PresentThread();
    PresentThread(const PresentThread &) = delete;
    PresentThread &operator=(const PresentThread &) = delete;

    // Waits until every frame posted is shown.
    void waitIdle();
    // The last job's status and how many frames the thread showed since the last call; both start again from zero.
    Outcome takeOutcome();

    // Makes the present objects for this swapchain: per-present command buffers, fences and acquire semaphores, and
    // one semaphore per swapchain image. Only while idle.
    bool prepare(Framework &framework, VkSwapchainKHR swapchain, size_t imageCount);
    // Hands `job` over (starting the thread on first use) and leaves an older job's buffers in it, for reuse.
    void post(Job &job);

    // Ends the thread once the job it is showing is done; a job posted but not started yet is dropped.
    void stop();
    // Destroys the present objects; the thread is stopped and the GPU done with them.
    void destroy();
    // Lets go of the present objects without destroying them (the device is gone).
    void forget();

  private:
    struct Slot {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkFence done = VK_NULL_HANDLE; // signalled once the slot's last blit has run
        VkSemaphore acquired = VK_NULL_HANDLE;
    };

    bool makeSlots(VkDevice device, uint32_t queueFamily);
    void destroySwapchainSemaphores();
    void run();
    Outcome show(const Job &job);
    int present(const Job &job, Slot &slot, VkImage source);

    // Present objects.
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::vector<Slot> slots_;
    uint32_t nextSlot_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    // One per swapchain image: its blit is done and it may be presented.
    std::vector<VkSemaphore> rendered_;
    uint32_t failuresReported_ = 0;

    // Hand-over, under mutex_.
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    Job job_;
    bool pending_ = false;
    bool stopping_ = false;
    Outcome outcome_;
};

} // namespace framegen
