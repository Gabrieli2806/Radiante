#pragma once

#include "prism/all_extern.hpp"

#include <string_view>

// GPU hang diagnostics. With RADIANTE_GPU_CHECKPOINTS set (and an NVIDIA driver), every GPU profiler mark also
// leaves a checkpoint in the command stream; when the device is lost, the last checkpoint each queue passed and
// the ones it had not yet reached are printed, which names the pass that hung.
namespace gpu::diagnostics {

bool checkpointsRequested();
void setCheckpointsEnabled(bool enabled);

void checkpoint(VkCommandBuffer commandBuffer, std::string_view name);
// Prints the checkpoints of a lost device's queue.
void reportLostDevice(VkQueue queue, const char *queueName);
// Checks a result for a lost device; reports the given queues once.
void onResult(VkResult result, VkQueue mainQueue, VkQueue secondaryQueue);

} // namespace gpu::diagnostics
