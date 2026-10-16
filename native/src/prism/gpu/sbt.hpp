#pragma once

#include "prism/all_extern.hpp"

#include <vector>

namespace gpu {
class DeviceLocalBuffer;
class HostVisibleBuffer;
class CommandBuffer;
class PhysicalDevice;
class Device;
class VMA;
class RayTracingPipeline;

// The shader binding table of one ray tracing pipeline for one frame slot. Ray generation and miss records are
// fixed and live in device memory; hit records follow the scene's instances and are written straight into mapped
// memory each frame, and only when they changed.
class SBT : public SharedObject<SBT> {
  public:
    SBT(std::shared_ptr<PhysicalDevice> physicalDevice,
        std::shared_ptr<Device> device,
        std::shared_ptr<VMA> vma,
        std::shared_ptr<RayTracingPipeline> pipeline,
        uint32_t missCount,
        uint32_t hitCount);

    // Copies the fixed records to device memory; once, before the first trace.
    void uploadStaticSBT(std::shared_ptr<CommandBuffer> commandBuffer);
    // One hit record per geometry slot, naming the hit group (index into the pipeline's hit groups) it runs.
    void setupHitSBT(const std::vector<uint32_t> &hitGroupIndices, std::shared_ptr<CommandBuffer> commandBuffer);

    VkStridedDeviceAddressRegionKHR &raygenRegion() {
        return raygenRegion_;
    }
    VkStridedDeviceAddressRegionKHR &missRegion() {
        return missRegion_;
    }
    VkStridedDeviceAddressRegionKHR &hitRegion() {
        return hitRegion_;
    }
    VkStridedDeviceAddressRegionKHR &callableRegion() {
        return callableRegion_;
    }

  private:
    std::shared_ptr<VMA> vma_;
    std::shared_ptr<Device> device_;

    uint32_t handleSize_;
    uint32_t stride_;
    uint32_t baseAlignment_;
    uint32_t missCount_;
    uint32_t hitGroupCount_;
    std::vector<uint8_t> handles_; // every group's handle, tightly packed: raygen, misses, hit groups

    VkStridedDeviceAddressRegionKHR raygenRegion_{};
    VkStridedDeviceAddressRegionKHR missRegion_{};
    VkStridedDeviceAddressRegionKHR hitRegion_{};
    VkStridedDeviceAddressRegionKHR callableRegion_{};

    std::shared_ptr<DeviceLocalBuffer> fixed_; // raygen record, then the miss records from baseAlignment
    std::shared_ptr<HostVisibleBuffer> hits_;
    std::vector<uint32_t> writtenHits_;
};
} // namespace gpu
