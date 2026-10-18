#pragma once

#include "prism/gpu/gpu.hpp"

#include <string>
#include <string_view>

// GPU time per stage of the world pipeline, from timestamp queries, averaged over 60 frames: shown on the F3 screen
// (summary()) and, with the environment variable RADIANTE_GPU_PROFILE set, logged to stdout every 300 frames
// ("[gpu profile]"). Off only when the query pool could not be made; mark() also leaves a GPU checkpoint when those
// are on (gpu::diagnostics).
namespace GpuProfiler {

bool enabled();

// RADIANTE_GPU_PROFILE is set: the per-stage averages are also logged.
bool verbose();

// The last averages, "13.14 ms (world 11.35, sharc_paths 0.32, DLSS 0.56)", or empty before the first.
std::string summary();

// At the start of a frame's world commands: collects what this frame slot measured the last time it was used (that
// frame has completed) and starts measuring anew.
void beginFrame(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandBuffer commandBuffer, uint32_t frameIndex);

// The GPU time since the previous mark, or since the frame began, is counted under name.
void mark(VkCommandBuffer commandBuffer, std::string_view name);

// Destroys the query pool; nothing may be running on the GPU. Measuring starts afresh if a frame begins again.
void release();

} // namespace GpuProfiler
