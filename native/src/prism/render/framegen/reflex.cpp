#include "prism/render/framegen/reflex.hpp"

#include "prism/util/logging.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <ostream>
#include <vector>

namespace framegen {
namespace {

// A lost signal (the swapchain rebuilt in the middle of a frame) must never stall the game for longer.
constexpr uint64_t kSleepTimeoutNs = 100'000'000ull;

// Everything Reflex keeps, behind one lock: the render thread and Java's swapchain hooks both call in.
struct State {
    std::mutex mutex;
    VkDevice device = VK_NULL_HANDLE;
    bool supported = false; // the extensions and present ids are enabled on the device
    bool enabled = false;   // the player's choice
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    // The sleep mode last given to the swapchain (0 off, 1 low latency); -1 until this swapchain has one.
    int appliedMode = -1;
    // Signalled by the driver when the CPU should start the next frame.
    VkSemaphore sleepSemaphore = VK_NULL_HANDLE;
    uint64_t sleepValue = 0;
    // The present id of the frame being made: its markers and its present carry it, which ties them together.
    uint64_t frameId = 0;
    // Chained into Minecraft's structures, so they live on until the create or present call reads them, right after
    // on the same thread.
    VkSwapchainLatencyCreateInfoNV swapchainLatency{VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV};
    VkPresentIdKHR presentId{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
    uint64_t presentIdValue = 0;

    bool usable() const {
        return supported && device != VK_NULL_HANDLE && vkSetLatencySleepModeNV != nullptr &&
               vkLatencySleepNV != nullptr && vkSetLatencyMarkerNV != nullptr;
    }

    // Reflex is on and has a swapchain to pace.
    bool pacing() const {
        return enabled && usable() && swapchain != VK_NULL_HANDLE;
    }

    bool ensureSleepSemaphore() {
        if (sleepSemaphore != VK_NULL_HANDLE) return true;
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        type.initialValue = 0;
        const VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
        if (vkCreateSemaphore(device, &info, nullptr, &sleepSemaphore) != VK_SUCCESS) {
            sleepSemaphore = VK_NULL_HANDLE;
            return false;
        }
        sleepValue = 0;
        return true;
    }
};

State g_state;

std::ostream &reflexOut() {
    return radiante::out() << "[Reflex] ";
}

// Puts `structure` at the front of the pNext chain that starts at `head`. A chain that holds it already (the same
// structure handed over twice) keeps it where it is: linking it a second time would close a loop.
template <typename Structure>
void chainFront(const void *&head, Structure &structure) {
    const auto *self = reinterpret_cast<const VkBaseInStructure *>(&structure);
    for (auto *link = static_cast<const VkBaseInStructure *>(head); link != nullptr; link = link->pNext) {
        if (link == self) return;
    }
    structure.pNext = head;
    head = &structure;
}

} // namespace

bool Reflex::deviceSupports(VkPhysicalDevice physicalDevice) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data());
    const auto has = [&extensions](const char *name) {
        return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties &extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
    };
    return has(VK_NV_LOW_LATENCY_2_EXTENSION_NAME) && has(VK_KHR_PRESENT_ID_EXTENSION_NAME);
}

void Reflex::setDevice(VkDevice device, bool enabled) {
    std::scoped_lock lock(g_state.mutex);
    g_state.device = device;
    g_state.supported = enabled;
    g_state.swapchain = VK_NULL_HANDLE;
    g_state.appliedMode = -1;
    // A semaphore of an earlier device went with it.
    g_state.sleepSemaphore = VK_NULL_HANDLE;
    g_state.sleepValue = 0;
    reflexOut() << (enabled ? "available (VK_NV_low_latency2)" : "not supported here") << std::endl;
}

bool Reflex::isSupported() {
    std::scoped_lock lock(g_state.mutex);
    return g_state.usable();
}

void Reflex::setEnabled(bool enabled) {
    std::scoped_lock lock(g_state.mutex);
    g_state.enabled = enabled;
}

bool Reflex::isActive() {
    std::scoped_lock lock(g_state.mutex);
    return g_state.pacing();
}

void Reflex::chainSwapchainCreate(void *createInfo) {
    std::scoped_lock lock(g_state.mutex);
    if (!g_state.usable() || createInfo == nullptr) return;
    // Always on where supported: switching Reflex is then only a sleep mode, never a new swapchain.
    g_state.swapchainLatency.latencyModeEnable = VK_TRUE;
    chainFront(static_cast<VkSwapchainCreateInfoKHR *>(createInfo)->pNext, g_state.swapchainLatency);
}

void Reflex::setSwapchain(VkSwapchainKHR swapchain) {
    std::scoped_lock lock(g_state.mutex);
    g_state.swapchain = swapchain;
    g_state.appliedMode = -1;
}

void Reflex::chainPresent(void *presentInfo) {
    std::scoped_lock lock(g_state.mutex);
    if (!g_state.pacing() || presentInfo == nullptr || g_state.frameId == 0) return;
    auto *info = static_cast<VkPresentInfoKHR *>(presentInfo);
    if (info->swapchainCount != 1) return;
    g_state.presentIdValue = g_state.frameId;
    g_state.presentId.swapchainCount = 1;
    g_state.presentId.pPresentIds = &g_state.presentIdValue;
    chainFront(info->pNext, g_state.presentId);
}

void Reflex::beginFrame() {
    std::scoped_lock lock(g_state.mutex);
    if (!g_state.usable() || g_state.swapchain == VK_NULL_HANDLE) return;

    const int mode = g_state.enabled ? 1 : 0;
    if (mode != g_state.appliedMode) {
        VkLatencySleepModeInfoNV sleepMode{VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV};
        sleepMode.lowLatencyMode = g_state.enabled ? VK_TRUE : VK_FALSE;
        sleepMode.lowLatencyBoost = g_state.enabled ? VK_TRUE : VK_FALSE;
        sleepMode.minimumIntervalUs = 0;
        if (vkSetLatencySleepModeNV(g_state.device, g_state.swapchain, &sleepMode) == VK_SUCCESS) {
            g_state.appliedMode = mode;
            reflexOut() << (g_state.enabled ? "low latency" : "off") << std::endl;
        }
    }
    if (!g_state.enabled || !g_state.ensureSleepSemaphore()) return;

    g_state.frameId++;
    // The sleep is what lowers latency: the driver signals the semaphore when the CPU should start the frame, so input
    // is read as late as the GPU allows instead of frames queueing up ahead of it.
    VkLatencySleepInfoNV sleep{VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV};
    sleep.signalSemaphore = g_state.sleepSemaphore;
    sleep.value = ++g_state.sleepValue;
    if (vkLatencySleepNV(g_state.device, g_state.swapchain, &sleep) != VK_SUCCESS) return;
    VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &g_state.sleepSemaphore;
    wait.pValues = &g_state.sleepValue;
    vkWaitSemaphores(g_state.device, &wait, kSleepTimeoutNs);
}

void Reflex::marker(int marker) {
    std::scoped_lock lock(g_state.mutex);
    if (!g_state.pacing() || g_state.frameId == 0) return;
    if (marker < VK_LATENCY_MARKER_SIMULATION_START_NV || marker > VK_LATENCY_MARKER_TRIGGER_FLASH_NV) return;
    VkSetLatencyMarkerInfoNV info{VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV};
    info.presentID = g_state.frameId;
    info.marker = static_cast<VkLatencyMarkerNV>(marker);
    vkSetLatencyMarkerNV(g_state.device, g_state.swapchain, &info);
}

} // namespace framegen
