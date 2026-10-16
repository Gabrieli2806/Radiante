#include "prism/gpu/diagnostics.hpp"

#include "prism/util/logging.hpp"

#include <atomic>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace gpu::diagnostics {
namespace {

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_reported{false};

// Checkpoint markers are pointers that must outlive the command buffers: names are kept for the process.
const char *intern(std::string_view name) {
    static std::mutex mutex;
    static std::set<std::string, std::less<>> names;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = names.find(name);
    if (it == names.end()) it = names.emplace(name).first;
    return it->c_str();
}

const char *stageName(VkPipelineStageFlagBits stage) {
    switch (stage) {
        case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT: return "top";
        case VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT: return "bottom";
        default: return "other";
    }
}

} // namespace

bool checkpointsRequested() {
    static const bool requested = std::getenv("RADIANTE_GPU_CHECKPOINTS") != nullptr;
    return requested;
}

void setCheckpointsEnabled(bool enabled) {
    g_enabled = enabled;
}

bool checkpointsEnabled() {
    return g_enabled;
}

void checkpoint(VkCommandBuffer commandBuffer, std::string_view name) {
    if (!g_enabled || vkCmdSetCheckpointNV == nullptr) return;
    vkCmdSetCheckpointNV(commandBuffer, intern(name));
}

void reportLostDevice(VkQueue queue, const char *queueName) {
    if (!g_enabled || queue == VK_NULL_HANDLE || vkGetQueueCheckpointDataNV == nullptr) return;
    uint32_t count = 0;
    vkGetQueueCheckpointDataNV(queue, &count, nullptr);
    std::vector<VkCheckpointDataNV> data(count, {VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV});
    vkGetQueueCheckpointDataNV(queue, &count, data.data());
    // Straight to stderr as well: the log the mod drains is read by a render thread that may be the one stuck.
    std::ostringstream text;
    text << "[gpu] device lost; " << queueName << " queue checkpoints (" << count << "):\n";
    for (const auto &point : data) {
        text << "[gpu]   " << stageName(point.stage) << " "
             << (point.pCheckpointMarker != nullptr ? static_cast<const char *>(point.pCheckpointMarker) : "?") << "\n";
    }
    std::cerr << text.str() << std::flush;
    radiante::err() << text.str() << std::flush;
}

void onResult(VkResult result, VkQueue mainQueue, VkQueue secondaryQueue) {
    if (result != VK_ERROR_DEVICE_LOST || g_reported.exchange(true)) return;
    reportLostDevice(mainQueue, "main");
    if (secondaryQueue != mainQueue) reportLostDevice(secondaryQueue, "secondary");
}

} // namespace gpu::diagnostics
