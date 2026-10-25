#pragma once

#include "prism/all_extern.hpp"

#include <span>
#include <vector>

namespace gpu {
class Buffer;
class DeviceLocalBuffer;
class HostVisibleBuffer;
class CommandBuffer;
class PhysicalDevice;
class Device;
class VMA;

// An acceleration structure living in a range of a buffer it keeps alive. Batch builds place many structures in one
// buffer. Destruction is deferred until no frame in flight can trace it (gpu::retire).
class AccelerationStructure : public SharedObject<AccelerationStructure> {
  public:
    AccelerationStructure(std::shared_ptr<Device> device,
                          VkAccelerationStructureTypeKHR type,
                          std::shared_ptr<DeviceLocalBuffer> buffer,
                          VkDeviceSize offset,
                          VkDeviceSize size);
    ~AccelerationStructure();

    VkAccelerationStructureKHR &handle() {
        return handle_;
    }
    VkDeviceAddress address() const {
        return address_;
    }
    VkDeviceSize size() const {
        return size_;
    }
    const std::shared_ptr<DeviceLocalBuffer> &buffer() const {
        return buffer_;
    }

  private:
    std::shared_ptr<Device> device_;
    std::shared_ptr<DeviceLocalBuffer> buffer_;
    VkDeviceSize size_;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    VkDeviceAddress address_ = 0;
};

using BLAS = AccelerationStructure;
using TLAS = AccelerationStructure;

// Storage for structures and scratch: device memory addressable by the GPU.
std::shared_ptr<DeviceLocalBuffer> allocateStructureStorage(const std::shared_ptr<Device> &device,
                                                            const std::shared_ptr<VMA> &vma,
                                                            VkDeviceSize size);
std::shared_ptr<DeviceLocalBuffer> allocateScratch(const std::shared_ptr<PhysicalDevice> &physicalDevice,
                                                   const std::shared_ptr<Device> &device,
                                                   const std::shared_ptr<VMA> &vma,
                                                   VkDeviceSize size);

// The geometry of one bottom-level structure and how it is built. Describe the geometry, size it, place it (in a
// buffer of its own or a shared one), then record the build with recordBlasBuilds.
class BlasBuild : public SharedObject<BlasBuild> {
  public:
    explicit BlasBuild(VkBuildAccelerationStructureFlagsKHR flags);

    // Indexed triangles with an explicit vertex format and stride. An empty draw becomes an empty geometry, so
    // geometry indices still line up with the hit groups.
    BlasBuild &triangles(VkDeviceAddress vertices,
                         VkFormat vertexFormat,
                         VkDeviceSize vertexStride,
                         uint32_t vertexCount,
                         VkDeviceAddress indices,
                         VkIndexType indexType,
                         uint32_t indexCount,
                         bool opaque);
    // Float3 positions at the start of each Vertex, 32-bit indices.
    template <typename Vertex>
    BlasBuild &
    triangles(VkDeviceAddress vertices, uint32_t vertexCount, VkDeviceAddress indices, uint32_t indexCount, bool opaque) {
        return triangles(vertices, VK_FORMAT_R32G32B32_SFLOAT, sizeof(Vertex), vertexCount, indices,
                         VK_INDEX_TYPE_UINT32, indexCount, opaque);
    }
    BlasBuild &empty();

    // The driver's sizes for this geometry (asked once).
    const VkAccelerationStructureBuildSizesInfoKHR &sizes(const std::shared_ptr<Device> &device);

    // Makes the structure in storage of its own.
    std::shared_ptr<BLAS> place(const std::shared_ptr<Device> &device, const std::shared_ptr<VMA> &vma);
    // Makes the structure at an offset of a shared buffer (256-byte aligned).
    std::shared_ptr<BLAS>
    placeIn(const std::shared_ptr<Device> &device, std::shared_ptr<DeviceLocalBuffer> buffer, VkDeviceSize offset);

    const std::shared_ptr<BLAS> &target() const {
        return target_;
    }
    VkBuildAccelerationStructureFlagsKHR flags() const {
        return flags_;
    }

  private:
    friend std::shared_ptr<DeviceLocalBuffer> recordBlasBuilds(const std::shared_ptr<CommandBuffer> &,
                                                               std::span<const std::shared_ptr<BlasBuild>>,
                                                               const std::shared_ptr<PhysicalDevice> &,
                                                               const std::shared_ptr<Device> &,
                                                               const std::shared_ptr<VMA> &);
    friend void recordBlasUpdates(const std::shared_ptr<CommandBuffer> &,
                                  std::span<const std::shared_ptr<BlasBuild>>,
                                  std::shared_ptr<DeviceLocalBuffer> &,
                                  const std::shared_ptr<PhysicalDevice> &,
                                  const std::shared_ptr<Device> &,
                                  const std::shared_ptr<VMA> &);

    VkAccelerationStructureBuildGeometryInfoKHR geometryInfo() const;

    VkBuildAccelerationStructureFlagsKHR flags_;
    std::vector<VkAccelerationStructureGeometryKHR> geometries_;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges_;
    VkAccelerationStructureBuildSizesInfoKHR sizes_{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    bool sized_ = false;
    std::shared_ptr<BLAS> target_;
};

// Records every placed build in one command with one scratch buffer. The scratch buffer is returned: hold it until
// the command buffer has run, or drop it when that buffer belongs to the frame (it is then retired with the frame).
[[nodiscard]] std::shared_ptr<DeviceLocalBuffer> recordBlasBuilds(const std::shared_ptr<CommandBuffer> &commandBuffer,
                                                                  std::span<const std::shared_ptr<BlasBuild>> builds,
                                                                  const std::shared_ptr<PhysicalDevice> &physicalDevice,
                                                                  const std::shared_ptr<Device> &device,
                                                                  const std::shared_ptr<VMA> &vma);

// Records an in-place update (a refit) of every structure, each built before with ALLOW_UPDATE from the same
// geometry description, whose vertices have since moved. `scratch` is kept between calls and only replaced (the old
// one retired with the frame) when it is too small; callers sharing it across frames order their updates with a
// barrier, as frames on one queue already are.
void recordBlasUpdates(const std::shared_ptr<CommandBuffer> &commandBuffer,
                       std::span<const std::shared_ptr<BlasBuild>> builds,
                       std::shared_ptr<DeviceLocalBuffer> &scratch,
                       const std::shared_ptr<PhysicalDevice> &physicalDevice,
                       const std::shared_ptr<Device> &device,
                       const std::shared_ptr<VMA> &vma);

// Bottom-level builds placed together in one buffer and recorded together. The batch holds the scratch memory of
// its last recording until it is destroyed.
class BlasBatch : public SharedObject<BlasBatch> {
  public:
    std::shared_ptr<BlasBuild> add(VkBuildAccelerationStructureFlagsKHR flags);
    // Sizes every build and places them in one buffer; the structures come back in the order they were added.
    std::vector<std::shared_ptr<BLAS>> place(const std::shared_ptr<Device> &device, const std::shared_ptr<VMA> &vma);
    void record(const std::shared_ptr<CommandBuffer> &commandBuffer,
                const std::shared_ptr<PhysicalDevice> &physicalDevice,
                const std::shared_ptr<Device> &device,
                const std::shared_ptr<VMA> &vma);

    VkDeviceSize storageBytes() const;
    bool empty() const {
        return builds_.empty();
    }

  private:
    std::vector<std::shared_ptr<BlasBuild>> builds_;
    std::shared_ptr<DeviceLocalBuffer> storage_;
    std::shared_ptr<DeviceLocalBuffer> scratch_;
};

// The top-level structure of one frame slot, rebuilt each frame from the instances added since clear(). Its
// instance, storage and scratch memory are kept and only grow, so a frame allocates nothing; the instances are
// written straight into mapped memory the build reads.
class TlasBuilder : public SharedObject<TlasBuilder> {
  public:
    TlasBuilder(std::shared_ptr<PhysicalDevice> physicalDevice, std::shared_ptr<Device> device, std::shared_ptr<VMA> vma);

    void clear() {
        instances_.clear();
    }
    void add(const VkTransformMatrixKHR &transform,
             uint32_t customIndex,
             uint8_t mask,
             uint32_t sbtOffset,
             VkGeometryInstanceFlagsKHR flags,
             VkDeviceAddress blas) {
        VkAccelerationStructureInstanceKHR &instance = instances_.emplace_back();
        instance.transform = transform;
        instance.instanceCustomIndex = customIndex & 0xFFFFFFu;
        instance.mask = mask;
        instance.instanceShaderBindingTableRecordOffset = sbtOffset & 0xFFFFFFu;
        instance.flags = flags & 0xFFu;
        instance.accelerationStructureReference = blas;
    }
    size_t count() const {
        return instances_.size();
    }
    void reserve(size_t count) {
        instances_.reserve(count);
    }

    // Writes the instances and records the build. The returned structure is the same object frame after frame
    // until it has to grow. Follow with a barrier before tracing.
    std::shared_ptr<TLAS> record(const std::shared_ptr<CommandBuffer> &commandBuffer);

  private:
    std::shared_ptr<PhysicalDevice> physicalDevice_;
    std::shared_ptr<Device> device_;
    std::shared_ptr<VMA> vma_;
    std::vector<VkAccelerationStructureInstanceKHR> instances_;
    std::shared_ptr<HostVisibleBuffer> instanceBuffer_;
    std::shared_ptr<DeviceLocalBuffer> scratch_;
    std::shared_ptr<TLAS> tlas_;
};
} // namespace gpu
