#pragma once

#include "prism/all_extern.hpp"

namespace framegen {

// NVIDIA Reflex through VK_NV_low_latency2, called on the driver directly: no Streamline, and the same code on Windows
// and Linux. Minecraft owns the swapchain, so its creation and its presents are extended from Java (ReflexSurfaceMixin)
// with the structures made here. Where the extension is missing (other vendors, old drivers) every call does nothing.
class Reflex {
  public:
    // Whether the GPU has the device extensions Reflex needs, asked while the device is made
    // (gpu::Device::createMerged), then the device and whether they were enabled on it.
    static bool deviceSupports(VkPhysicalDevice physicalDevice);
    static void setDevice(VkDevice device, bool enabled);
    static bool isSupported();

    // The player's choice; it applies from the next frame.
    static void setEnabled(bool enabled);

    // Chains VkSwapchainLatencyCreateInfoNV into Minecraft's VkSwapchainCreateInfoKHR (given by address).
    static void chainSwapchainCreate(void *createInfo);
    // The swapchain Minecraft presents to; VK_NULL_HANDLE while it is rebuilt.
    static void setSwapchain(VkSwapchainKHR swapchain);
    // Chains VkPresentIdKHR with the open frame's id into Minecraft's VkPresentInfoKHR (given by address).
    static void chainPresent(void *presentInfo);

    // Once per client frame, before input is read: sleeps just long enough that frames never queue up ahead of the GPU.
    static void beginFrame();
    // A VkLatencyMarkerNV for the open frame (simulation, render submit and present, each start and end).
    static void marker(int marker);
};

} // namespace framegen
