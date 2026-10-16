#pragma once

#include "prism/gpu/gpu.hpp"

#include <string>
#include <string_view>

// Development aid: GPU time per stage of the world pipeline, from timestamp queries, logged to stdout every few hundred
// frames as averages ("[gpu profile]"). Off unless the environment variable RADIANTE_GPU_PROFILE is set. While off,
// every call returns at once, except that mark() still leaves a GPU checkpoint when those are on (gpu::diagnostics).
namespace GpuProfiler {

bool enabled();

// At the start of a frame's world commands: collects what this frame slot measured the last time it was used (that
// frame has completed) and starts measuring anew.
void beginFrame(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandBuffer commandBuffer, uint32_t frameIndex);

// The GPU time since the previous mark, or since the frame began, is counted under name.
void mark(VkCommandBuffer commandBuffer, std::string_view name);

// Destroys the query pool; nothing may be running on the GPU. Measuring starts afresh if a frame begins again.
void release();

} // namespace GpuProfiler
