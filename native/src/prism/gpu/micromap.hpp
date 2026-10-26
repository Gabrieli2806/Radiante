#pragma once

#include "prism/all_extern.hpp"

#include <atomic>
#include <span>
#include <vector>

namespace gpu {
class CommandBuffer;
class DeviceLocalBuffer;
class PhysicalDevice;
class Device;
class VMA;

// Whether the device was made with opacity micromaps (VK_EXT_opacity_micromap). Set once the renderer's device wrapper
// exists; the stages and buffer usages of the extension may only be named where it is.
bool opacityMicromapsEnabled();
void setOpacityMicromapsEnabled(bool enabled);

// An opacity micromap: per micro-triangle of each of its triangles, whether a ray passes there (transparent), stops
// there (opaque), or has to ask the any-hit shader (unknown). Bottom-level structures reference it by handle, so it is
// kept alive as long as any structure built with it may be traced; destruction is deferred like theirs (gpu::retire).
class Micromap : public SharedObject<Micromap> {
  public:
    Micromap(std::shared_ptr<Device> device, std::shared_ptr<DeviceLocalBuffer> buffer, VkDeviceSize size);
    ~Micromap();

    VkMicromapEXT handle() const {
        return handle_;
    }
    VkDeviceSize size() const {
        return size_;
    }

    // Bytes held by every micromap alive, for the memory report.
    static uint64_t liveBytes() {
        return liveBytes_.load(std::memory_order_relaxed);
    }

  private:
    static std::atomic<uint64_t> liveBytes_;

    std::shared_ptr<Device> device_;
    std::shared_ptr<DeviceLocalBuffer> buffer_;
    VkDeviceSize size_;
    VkMicromapEXT handle_ = VK_NULL_HANDLE;
};

// The build of one micromap from data already on the GPU: the packed micro-triangle states and the array of
// VkMicromapTriangleEXT that says where each triangle's states start, at what subdivision level and in which format.
// Placed when it is made; recorded by recordBlasBuilds ahead of the bottom-level builds that use it.
class MicromapBuild : public SharedObject<MicromapBuild> {
  public:
    // `usage` counts the micromap's triangles per subdivision level and format. Both addresses are 256-byte aligned.
    MicromapBuild(const std::shared_ptr<Device> &device,
                  const std::shared_ptr<VMA> &vma,
                  VkDeviceAddress data,
                  VkDeviceAddress triangles,
                  std::vector<VkMicromapUsageEXT> usage);

    const std::shared_ptr<Micromap> &micromap() const {
        return micromap_;
    }
    bool recorded() const {
        return recorded_;
    }

  private:
    friend VkDeviceSize micromapScratchSize(std::span<MicromapBuild *const>, VkDeviceSize);
    friend void recordMicromapBuilds(const std::shared_ptr<CommandBuffer> &,
                                     std::span<MicromapBuild *const>,
                                     VkDeviceAddress,
                                     VkDeviceSize);

    VkMicromapBuildInfoEXT buildInfo() const;

    VkDeviceAddress data_;
    VkDeviceAddress triangles_;
    std::vector<VkMicromapUsageEXT> usage_;
    VkMicromapBuildSizesInfoEXT sizes_{VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT};
    std::shared_ptr<Micromap> micromap_;
    bool recorded_ = false;
};

// The scratch memory recordMicromapBuilds needs for these builds, each part starting on `alignment`.
VkDeviceSize micromapScratchSize(std::span<MicromapBuild *const> builds, VkDeviceSize alignment);
// Records the given builds in one command, using scratch memory from `scratch` on (micromapScratchSize bytes, which the
// caller keeps until the command buffer has run), then a barrier that makes the micromaps readable by acceleration
// structure builds.
void recordMicromapBuilds(const std::shared_ptr<CommandBuffer> &commandBuffer,
                          std::span<MicromapBuild *const> builds,
                          VkDeviceAddress scratch,
                          VkDeviceSize alignment);
} // namespace gpu
