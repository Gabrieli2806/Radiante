#include "prism/render/gpu_profiler.hpp"

#include "prism/gpu/diagnostics.hpp"

#include <array>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace GpuProfiler {
namespace {

// Frame slots measured apart; the frame index is taken modulo this, so it only has to exceed the frames in flight.
constexpr uint32_t SLOTS = 8;
// Timestamps one frame can write; marks past that are not timed.
constexpr uint32_t PER_SLOT = 96;
// Frames averaged into one summary (the F3 line); every fifth summary is also logged when RADIANTE_GPU_PROFILE is set.
constexpr uint32_t REPORT_FRAMES = 60;
constexpr uint32_t LOG_EVERY = 5;

struct NameHash {
    using is_transparent = void;
    size_t operator()(std::string_view name) const noexcept {
        return std::hash<std::string_view>{}(name);
    }
};

struct State {
    // The query pool could not be made: profiling stays off.
    bool failed = false;
    VkDevice device = VK_NULL_HANDLE;
    VkQueryPool pool = VK_NULL_HANDLE;
    double periodMs = 1e-6;
    uint32_t slot = 0;
    bool active = false;
    // Per slot, the name each timestamp written there was marked with, as an index into names.
    std::array<std::vector<uint32_t>, SLOTS> marks;
    // Every name marked so far, kept once: after its first frame a mark allocates nothing.
    std::vector<std::string> names;
    std::unordered_map<std::string, uint32_t, NameHash, std::equal_to<>> ids;
    // Since the last report: GPU time per name, and the names in the order they were first measured.
    std::vector<double> sums;
    std::vector<bool> listed;
    std::vector<uint32_t> order;
    double totalMs = 0.0;
    uint32_t frames = 0;
    uint32_t reports = 0;
    // The last summary, for the F3 screen. Written and read on the render thread, like everything here.
    std::string summary;
};

State &state() {
    static State s;
    return s;
}

uint32_t nameId(State &s, std::string_view name) {
    if (auto found = s.ids.find(name); found != s.ids.end()) return found->second;
    const auto id = static_cast<uint32_t>(s.names.size());
    s.names.emplace_back(name);
    s.ids.emplace(s.names.back(), id);
    s.sums.push_back(0.0);
    s.listed.push_back(false);
    return id;
}

// "struct DLSSModuleContext" -> "DLSS", "rt:world" -> "world": the short name the F3 line shows.
std::string_view shortName(std::string_view name) {
    for (std::string_view prefix : {"struct ", "class ", "rt:"}) {
        if (name.starts_with(prefix)) name.remove_prefix(prefix.size());
    }
    if (name.ends_with("ModuleContext")) name.remove_suffix(std::string_view("ModuleContext").size());
    return name;
}

void report(State &s) {
    std::ostringstream brief;
    brief << std::fixed << std::setprecision(2) << s.totalMs / s.frames << " ms";
    std::ostringstream line;
    line << "[gpu profile] " << std::fixed << std::setprecision(3) << s.totalMs / s.frames << " ms in all:";
    bool first = true;
    for (uint32_t id : s.order) {
        const double ms = s.sums[id] / s.frames;
        if (ms >= 0.005) line << " " << s.names[id] << "=" << ms;
        if (ms >= 0.05 && s.names[id] != "rt:before") {
            brief << (first ? " (" : ", ") << shortName(s.names[id]) << " " << ms;
            first = false;
        }
    }
    if (!first) brief << ")";
    s.summary = brief.str();
    if (verbose() && ++s.reports % LOG_EVERY == 0) std::cout << line.str() << std::endl;

    for (uint32_t id : s.order) {
        s.sums[id] = 0.0;
        s.listed[id] = false;
    }
    s.order.clear();
    s.totalMs = 0.0;
    s.frames = 0;
}

// Adds what the slot measured the last time it was used. Results that are not available yet are skipped.
void collect(State &s, uint32_t slot) {
    const std::vector<uint32_t> &marks = s.marks[slot];
    const auto count = static_cast<uint32_t>(marks.size());
    if (count < 2) return;

    std::array<uint64_t, PER_SLOT> stamps;
    VkResult result = vkGetQueryPoolResults(s.device, s.pool, slot * PER_SLOT, count, sizeof(stamps), stamps.data(),
                                            sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS) return;

    for (uint32_t i = 1; i < count; i++) {
        const uint32_t id = marks[i];
        if (!s.listed[id]) {
            s.listed[id] = true;
            s.order.push_back(id);
        }
        s.sums[id] += double(stamps[i] - stamps[i - 1]) * s.periodMs;
    }
    s.totalMs += double(stamps[count - 1] - stamps[0]) * s.periodMs;
    if (++s.frames >= REPORT_FRAMES) report(s);
}

bool createPool(State &s, VkDevice device, VkPhysicalDevice physicalDevice) {
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = SLOTS * PER_SLOT;
    if (vkCreateQueryPool(device, &info, nullptr, &s.pool) != VK_SUCCESS) {
        s.pool = VK_NULL_HANDLE;
        return false;
    }
    s.device = device;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    s.periodMs = double(properties.limits.timestampPeriod) * 1e-6;
    return true;
}

} // namespace

bool verbose() {
    static const bool requested = std::getenv("RADIANTE_GPU_PROFILE") != nullptr;
    return requested;
}

bool enabled() {
    return !state().failed;
}

std::string summary() {
    return state().summary;
}

void beginFrame(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!enabled()) return;
    State &s = state();
    if (s.pool == VK_NULL_HANDLE && !createPool(s, device, physicalDevice)) {
        s.failed = true;
        return;
    }

    const uint32_t slot = frameIndex % SLOTS;
    collect(s, slot);
    vkCmdResetQueryPool(commandBuffer, s.pool, slot * PER_SLOT, PER_SLOT);
    s.slot = slot;
    s.marks[slot].clear();
    s.active = true;
    mark(commandBuffer, "start");
}

void mark(VkCommandBuffer commandBuffer, std::string_view name) {
    gpu::diagnostics::checkpoint(commandBuffer, name);
    if (!enabled()) return;
    State &s = state();
    std::vector<uint32_t> &marks = s.marks[s.slot];
    if (!s.active || marks.size() >= PER_SLOT) return;
    vkCmdWriteTimestamp2(commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, s.pool,
                         s.slot * PER_SLOT + static_cast<uint32_t>(marks.size()));
    marks.push_back(nameId(s, name));
}

void release() {
    State &s = state();
    if (s.pool != VK_NULL_HANDLE) vkDestroyQueryPool(s.device, s.pool, nullptr);
    s.pool = VK_NULL_HANDLE;
    s.device = VK_NULL_HANDLE;
    s.active = false;
    for (auto &marks : s.marks) marks.clear();
}

} // namespace GpuProfiler
