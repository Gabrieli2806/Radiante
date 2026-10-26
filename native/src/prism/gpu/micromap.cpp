#include "prism/gpu/micromap.hpp"

#include "prism/gpu/buffer.hpp"
#include "prism/gpu/command.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/retire.hpp"
#include "prism/gpu/vma.hpp"

#include <algorithm>
#include <stdexcept>

namespace gpu {
namespace {

// Micromap storage, its build data and its triangle array all start on 256-byte boundaries.
constexpr VkDeviceSize kMicromapAlignment = 256;

std::atomic<bool> g_micromapsEnabled{false};

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

bool opacityMicromapsEnabled() {
    return g_micromapsEnabled.load(std::memory_order_relaxed);
}

void setOpacityMicromapsEnabled(bool enabled) {
    g_micromapsEnabled.store(enabled, std::memory_order_relaxed);
}

// ---- Micromap

std::atomic<uint64_t> Micromap::liveBytes_{0};

Micromap::Micromap(std::shared_ptr<Device> device, std::shared_ptr<DeviceLocalBuffer> buffer, VkDeviceSize size)
    : device_(std::move(device)), buffer_(std::move(buffer)), size_(size) {
    VkMicromapCreateInfoEXT info{VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT};
    info.buffer = buffer_->vkBuffer();
    info.offset = 0;
    info.size = size;
    info.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    if (vkCreateMicromapEXT(device_->vkDevice(), &info, nullptr, &handle_) != VK_SUCCESS) {
        throw std::runtime_error("opacity micromap could not be made");
    }
    liveBytes_.fetch_add(buffer_->size(), std::memory_order_relaxed);
}

Micromap::~Micromap() {
    liveBytes_.fetch_sub(buffer_->size(), std::memory_order_relaxed);
    // As with acceleration structures, the buffer goes with the handle.
    retire::later([device = device_, handle = handle_, buffer = std::move(buffer_)]() mutable {
        vkDestroyMicromapEXT(device->vkDevice(), handle, nullptr);
        buffer.reset();
    });
}

// ---- MicromapBuild

MicromapBuild::MicromapBuild(const std::shared_ptr<Device> &device,
                             const std::shared_ptr<VMA> &vma,
                             VkDeviceAddress data,
                             VkDeviceAddress triangles,
                             std::vector<VkMicromapUsageEXT> usage)
    : data_(data), triangles_(triangles), usage_(std::move(usage)) {
    const VkMicromapBuildInfoEXT info = buildInfo();
    vkGetMicromapBuildSizesEXT(device->vkDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &sizes_);
    auto storage = DeviceLocalBuffer::create(
        vma, device, false, std::max<VkDeviceSize>(sizes_.micromapSize, 1),
        VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 0,
        VMA_MEMORY_USAGE_GPU_ONLY, kMicromapAlignment);
    micromap_ = Micromap::create(device, std::move(storage), sizes_.micromapSize);
}

VkMicromapBuildInfoEXT MicromapBuild::buildInfo() const {
    VkMicromapBuildInfoEXT info{VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT};
    info.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    info.flags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    info.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    info.usageCountsCount = static_cast<uint32_t>(usage_.size());
    info.pUsageCounts = usage_.data();
    info.data.deviceAddress = data_;
    info.triangleArray.deviceAddress = triangles_;
    info.triangleArrayStride = sizeof(VkMicromapTriangleEXT);
    return info;
}

VkDeviceSize micromapScratchSize(std::span<MicromapBuild *const> builds, VkDeviceSize alignment) {
    alignment = std::max(alignment, kMicromapAlignment);
    VkDeviceSize total = 0;
    for (const MicromapBuild *build : builds) {
        total = alignUp(total + std::max<VkDeviceSize>(build->sizes_.buildScratchSize, 1), alignment);
    }
    return total;
}

void recordMicromapBuilds(const std::shared_ptr<CommandBuffer> &commandBuffer,
                          std::span<MicromapBuild *const> builds,
                          VkDeviceAddress scratch,
                          VkDeviceSize alignment) {
    if (builds.empty()) return;
    alignment = std::max(alignment, kMicromapAlignment);
    std::vector<VkMicromapBuildInfoEXT> infos;
    infos.reserve(builds.size());
    VkDeviceSize offset = 0;
    for (MicromapBuild *build : builds) {
        VkMicromapBuildInfoEXT info = build->buildInfo();
        info.dstMicromap = build->micromap_->handle();
        info.scratchData.deviceAddress = scratch + offset;
        offset = alignUp(offset + std::max<VkDeviceSize>(build->sizes_.buildScratchSize, 1), alignment);
        infos.push_back(info);
        build->recorded_ = true;
    }
    vkCmdBuildMicromapsEXT(commandBuffer->vkCommandBuffer(), static_cast<uint32_t>(infos.size()), infos.data());
    commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT, VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT,
                                 VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                 VK_ACCESS_2_MICROMAP_READ_BIT_EXT);
}

} // namespace gpu
