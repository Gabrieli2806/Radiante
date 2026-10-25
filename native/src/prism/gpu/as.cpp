#include "prism/gpu/as.hpp"

#include "prism/gpu/buffer.hpp"
#include "prism/gpu/command.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/physical_device.hpp"
#include "prism/gpu/retire.hpp"
#include "prism/gpu/vma.hpp"

#include <algorithm>
#include <stdexcept>

namespace gpu {
namespace {

constexpr VkDeviceSize kStructureAlignment = 256; // required offset alignment of a structure in its buffer

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

VkDeviceSize scratchAlignment(const std::shared_ptr<PhysicalDevice> &physicalDevice) {
    return std::max<VkDeviceSize>(
        1, physicalDevice->accelerationStructProperties().minAccelerationStructureScratchOffsetAlignment);
}

} // namespace

// ---- AccelerationStructure

AccelerationStructure::AccelerationStructure(std::shared_ptr<Device> device,
                                             VkAccelerationStructureTypeKHR type,
                                             std::shared_ptr<DeviceLocalBuffer> buffer,
                                             VkDeviceSize offset,
                                             VkDeviceSize size)
    : device_(std::move(device)), buffer_(std::move(buffer)), size_(size) {
    VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    info.buffer = buffer_->vkBuffer();
    info.offset = offset;
    info.size = size;
    info.type = type;
    if (vkCreateAccelerationStructureKHR(device_->vkDevice(), &info, nullptr, &handle_) != VK_SUCCESS) {
        throw std::runtime_error("acceleration structure could not be made");
    }
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addressInfo.accelerationStructure = handle_;
    address_ = vkGetAccelerationStructureDeviceAddressKHR(device_->vkDevice(), &addressInfo);
}

AccelerationStructure::~AccelerationStructure() {
    // The buffer goes with the handle: retiring it separately could free the memory first.
    retire::later([device = device_, handle = handle_, buffer = std::move(buffer_)]() mutable {
        vkDestroyAccelerationStructureKHR(device->vkDevice(), handle, nullptr);
        buffer.reset();
    });
}

std::shared_ptr<DeviceLocalBuffer> allocateStructureStorage(const std::shared_ptr<Device> &device,
                                                            const std::shared_ptr<VMA> &vma,
                                                            VkDeviceSize size) {
    return DeviceLocalBuffer::create(
        vma, device, false, size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 0,
        VMA_MEMORY_USAGE_GPU_ONLY, kStructureAlignment);
}

std::shared_ptr<DeviceLocalBuffer> allocateScratch(const std::shared_ptr<PhysicalDevice> &physicalDevice,
                                                   const std::shared_ptr<Device> &device,
                                                   const std::shared_ptr<VMA> &vma,
                                                   VkDeviceSize size) {
    return DeviceLocalBuffer::create(vma, device, false, size,
                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 0,
                                     VMA_MEMORY_USAGE_GPU_ONLY, scratchAlignment(physicalDevice));
}

// ---- BlasBuild

BlasBuild::BlasBuild(VkBuildAccelerationStructureFlagsKHR flags) : flags_(flags) {}

BlasBuild &BlasBuild::triangles(VkDeviceAddress vertices,
                                VkFormat vertexFormat,
                                VkDeviceSize vertexStride,
                                uint32_t vertexCount,
                                VkDeviceAddress indices,
                                VkIndexType indexType,
                                uint32_t indexCount,
                                bool opaque) {
    if (vertexCount == 0 || indexCount < 3) return empty();

    VkAccelerationStructureGeometryKHR &geometry =
        geometries_.emplace_back(VkAccelerationStructureGeometryKHR{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR});
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
    auto &data = geometry.geometry.triangles;
    data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    data.vertexFormat = vertexFormat;
    data.vertexData.deviceAddress = vertices;
    data.vertexStride = vertexStride;
    data.maxVertex = vertexCount - 1;
    data.indexType = indexType;
    data.indexData.deviceAddress = indices;

    ranges_.push_back({indexCount / 3, 0, 0, 0});
    sized_ = false;
    return *this;
}

BlasBuild &BlasBuild::empty() {
    VkAccelerationStructureGeometryKHR &geometry =
        geometries_.emplace_back(VkAccelerationStructureGeometryKHR{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR});
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto &data = geometry.geometry.triangles;
    data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    data.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT; // required, never read
    data.indexType = VK_INDEX_TYPE_NONE_KHR;

    ranges_.push_back({0, 0, 0, 0});
    sized_ = false;
    return *this;
}

VkAccelerationStructureBuildGeometryInfoKHR BlasBuild::geometryInfo() const {
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.flags = flags_;
    info.geometryCount = static_cast<uint32_t>(geometries_.size());
    info.pGeometries = geometries_.data();
    return info;
}

const VkAccelerationStructureBuildSizesInfoKHR &BlasBuild::sizes(const std::shared_ptr<Device> &device) {
    if (!sized_) {
        std::vector<uint32_t> counts(ranges_.size());
        for (size_t i = 0; i < ranges_.size(); i++) counts[i] = ranges_[i].primitiveCount;
        VkAccelerationStructureBuildGeometryInfoKHR info = geometryInfo();
        sizes_ = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(device->vkDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &info, counts.data(), &sizes_);
        sized_ = true;
    }
    return sizes_;
}

std::shared_ptr<BLAS> BlasBuild::place(const std::shared_ptr<Device> &device, const std::shared_ptr<VMA> &vma) {
    VkDeviceSize size = sizes(device).accelerationStructureSize;
    return placeIn(device, allocateStructureStorage(device, vma, size), 0);
}

std::shared_ptr<BLAS> BlasBuild::placeIn(const std::shared_ptr<Device> &device,
                                         std::shared_ptr<DeviceLocalBuffer> buffer,
                                         VkDeviceSize offset) {
    target_ = BLAS::create(device, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, std::move(buffer), offset,
                           sizes(device).accelerationStructureSize);
    return target_;
}

std::shared_ptr<DeviceLocalBuffer> recordBlasBuilds(const std::shared_ptr<CommandBuffer> &commandBuffer,
                                                    std::span<const std::shared_ptr<BlasBuild>> builds,
                                                    const std::shared_ptr<PhysicalDevice> &physicalDevice,
                                                    const std::shared_ptr<Device> &device,
                                                    const std::shared_ptr<VMA> &vma) {
    const VkDeviceSize alignment = scratchAlignment(physicalDevice);
    std::vector<VkDeviceSize> offsets;
    offsets.reserve(builds.size());
    VkDeviceSize total = 0;
    for (const auto &build : builds) {
        if (build == nullptr || build->target_ == nullptr) {
            throw std::logic_error("bottom-level build recorded before it was placed");
        }
        offsets.push_back(total);
        total = alignUp(total + build->sizes(device).buildScratchSize, alignment);
    }
    if (builds.empty()) return nullptr;

    auto scratch = allocateScratch(physicalDevice, device, vma, total);
    const VkDeviceAddress scratchAddress = scratch->bufferAddress();

    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR *> ranges;
    infos.reserve(builds.size());
    ranges.reserve(builds.size());
    for (size_t i = 0; i < builds.size(); i++) {
        VkAccelerationStructureBuildGeometryInfoKHR info = builds[i]->geometryInfo();
        info.dstAccelerationStructure = builds[i]->target_->handle();
        info.scratchData.deviceAddress = scratchAddress + offsets[i];
        infos.push_back(info);
        ranges.push_back(builds[i]->ranges_.data());
    }
    vkCmdBuildAccelerationStructuresKHR(commandBuffer->vkCommandBuffer(), static_cast<uint32_t>(infos.size()),
                                        infos.data(), ranges.data());
    return scratch;
}

void recordBlasUpdates(const std::shared_ptr<CommandBuffer> &commandBuffer,
                       std::span<const std::shared_ptr<BlasBuild>> builds,
                       std::shared_ptr<DeviceLocalBuffer> &scratch,
                       const std::shared_ptr<PhysicalDevice> &physicalDevice,
                       const std::shared_ptr<Device> &device,
                       const std::shared_ptr<VMA> &vma) {
    if (builds.empty()) return;
    const VkDeviceSize alignment = scratchAlignment(physicalDevice);
    std::vector<VkDeviceSize> offsets;
    offsets.reserve(builds.size());
    VkDeviceSize total = 0;
    for (const auto &build : builds) {
        if (build == nullptr || build->target_ == nullptr ||
            (build->flags_ & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR) == 0) {
            throw std::logic_error("bottom-level update of a structure that was not built to be updated");
        }
        offsets.push_back(total);
        total = alignUp(total + std::max<VkDeviceSize>(build->sizes(device).updateScratchSize, 1), alignment);
    }
    // Half again as much room, so a few more sections coming into reach do not reallocate every frame.
    if (scratch == nullptr || scratch->size() < total) {
        scratch = allocateScratch(physicalDevice, device, vma, total + total / 2);
    }
    const VkDeviceAddress scratchAddress = scratch->bufferAddress();

    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR *> ranges;
    infos.reserve(builds.size());
    ranges.reserve(builds.size());
    for (size_t i = 0; i < builds.size(); i++) {
        VkAccelerationStructureBuildGeometryInfoKHR info = builds[i]->geometryInfo();
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        info.srcAccelerationStructure = builds[i]->target_->handle();
        info.dstAccelerationStructure = builds[i]->target_->handle();
        info.scratchData.deviceAddress = scratchAddress + offsets[i];
        infos.push_back(info);
        ranges.push_back(builds[i]->ranges_.data());
    }
    vkCmdBuildAccelerationStructuresKHR(commandBuffer->vkCommandBuffer(), static_cast<uint32_t>(infos.size()),
                                        infos.data(), ranges.data());
}

// ---- BlasBatch

std::shared_ptr<BlasBuild> BlasBatch::add(VkBuildAccelerationStructureFlagsKHR flags) {
    return builds_.emplace_back(BlasBuild::create(flags));
}

std::vector<std::shared_ptr<BLAS>> BlasBatch::place(const std::shared_ptr<Device> &device,
                                                     const std::shared_ptr<VMA> &vma) {
    std::vector<VkDeviceSize> offsets;
    offsets.reserve(builds_.size());
    VkDeviceSize total = 0;
    for (const auto &build : builds_) {
        offsets.push_back(total);
        total = alignUp(total + build->sizes(device).accelerationStructureSize, kStructureAlignment);
    }

    std::vector<std::shared_ptr<BLAS>> placed;
    if (builds_.empty()) return placed;
    storage_ = allocateStructureStorage(device, vma, total);
    placed.reserve(builds_.size());
    for (size_t i = 0; i < builds_.size(); i++) placed.push_back(builds_[i]->placeIn(device, storage_, offsets[i]));
    return placed;
}

void BlasBatch::record(const std::shared_ptr<CommandBuffer> &commandBuffer,
                       const std::shared_ptr<PhysicalDevice> &physicalDevice,
                       const std::shared_ptr<Device> &device,
                       const std::shared_ptr<VMA> &vma) {
    scratch_ = recordBlasBuilds(commandBuffer, builds_, physicalDevice, device, vma);
}

VkDeviceSize BlasBatch::storageBytes() const {
    return storage_ != nullptr ? storage_->size() : 0;
}

// ---- TlasBuilder

TlasBuilder::TlasBuilder(std::shared_ptr<PhysicalDevice> physicalDevice,
                         std::shared_ptr<Device> device,
                         std::shared_ptr<VMA> vma)
    : physicalDevice_(std::move(physicalDevice)), device_(std::move(device)), vma_(std::move(vma)) {}

std::shared_ptr<TLAS> TlasBuilder::record(const std::shared_ptr<CommandBuffer> &commandBuffer) {
    if (instances_.empty()) return nullptr;
    const size_t count = instances_.size();
    const size_t bytes = count * sizeof(VkAccelerationStructureInstanceKHR);

    // Room for half again as many instances, so a growing world does not reallocate every frame.
    if (instanceBuffer_ == nullptr || instanceBuffer_->size() < bytes) {
        const size_t capacity = count + count / 2 + 64;
        instanceBuffer_ = HostVisibleBuffer::create(
            vma_, device_, capacity * sizeof(VkAccelerationStructureInstanceKHR),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            16);
    }
    instanceBuffer_->uploadToBuffer(instances_.data(), bytes, 0);

    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = instanceBuffer_->bufferAddress();

    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.geometryCount = 1;
    info.pGeometries = &geometry;

    // Sized for the buffer's capacity: the structure is reused for every count up to it.
    const uint32_t capacity =
        static_cast<uint32_t>(instanceBuffer_->size() / sizeof(VkAccelerationStructureInstanceKHR));
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(device_->vkDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info,
                                            &capacity, &sizes);
    if (tlas_ == nullptr || tlas_->size() < sizes.accelerationStructureSize) {
        tlas_ = TLAS::create(device_, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
                             allocateStructureStorage(device_, vma_, sizes.accelerationStructureSize), 0,
                             sizes.accelerationStructureSize);
    }
    if (scratch_ == nullptr || scratch_->size() < sizes.buildScratchSize) {
        scratch_ = allocateScratch(physicalDevice_, device_, vma_, sizes.buildScratchSize);
    }

    info.dstAccelerationStructure = tlas_->handle();
    info.scratchData.deviceAddress = scratch_->bufferAddress();
    VkAccelerationStructureBuildRangeInfoKHR range{static_cast<uint32_t>(count), 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(commandBuffer->vkCommandBuffer(), 1, &info, &ranges);
    return tlas_;
}

} // namespace gpu
