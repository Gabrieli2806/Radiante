#include "prism/render/mc_depth.hpp"

#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"

#include <array>

namespace {

// The push constants of mc_depth.comp.
struct DepthConstants {
    float zScale;
    float zOffset;
    float wScale;
    float wOffset;
    uint32_t sourceWidth;
    uint32_t sourceHeight;
    uint32_t targetWidth;
    uint32_t targetHeight;
};
static_assert(sizeof(DepthConstants) == 32);

std::shared_ptr<gpu::DescriptorTable> makeTable(const std::shared_ptr<gpu::Device> &device) {
    const auto storageImage = [](uint32_t binding) {
        return VkDescriptorSetLayoutBinding{
            .binding = binding,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    };
    return gpu::DescriptorTableBuilder{}
        .beginDescriptorLayoutSet()
        .beginDescriptorLayoutSetBinding()
        .defineDescriptorLayoutSetBinding(storageImage(0)) // the first hit depth
        .defineDescriptorLayoutSetBinding(storageImage(1)) // the converted depth
        .endDescriptorLayoutSetBinding()
        .endDescriptorLayoutSet()
        .definePushConstant({
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(DepthConstants),
        })
        .build(device);
}

VkImageMemoryBarrier2 toGeneral(gpu::Image &image, VkAccessFlags2 shaderAccess) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = shaderAccess;
    barrier.oldLayout = image.state().layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.vkImage();
    barrier.subresourceRange = gpu::wholeColorSubresourceRange;
    return barrier;
}

} // namespace

void McDepthWriter::reset() {
    sizes_.clear();
    pipeline_ = nullptr;
}

McDepthWriter::SizedResources &
McDepthWriter::resourcesFor(Framework &framework, uint32_t width, uint32_t height, uint32_t frameCount) {
    for (SizedResources &resources : sizes_) {
        if (resources.width == width && resources.height == height && resources.tables.size() == frameCount) {
            return resources;
        }
    }
    // The oldest size makes room; frames in flight keep what they use until they end (it retires).
    if (sizes_.size() >= MAX_SIZES) sizes_.erase(sizes_.begin());

    SizedResources &resources = sizes_.emplace_back();
    resources.width = width;
    resources.height = height;
    gpu::ImageDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = VK_FORMAT_R32_SFLOAT;
    desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    resources.depth = gpu::DeviceLocalImage::create(framework.device(), framework.vma(), desc);
    resources.transfer =
        gpu::DeviceLocalBuffer::create(framework.vma(), framework.device(), false,
                                       static_cast<size_t>(width) * height * sizeof(float),
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    resources.tables.reserve(frameCount);
    for (uint32_t i = 0; i < frameCount; i++) resources.tables.push_back(makeTable(framework.device()));

    if (pipeline_ == nullptr) {
        // Every size's tables have the same layout as these.
        auto shader = gpu::Shader::create(framework.device(),
                                          (Renderer::folderPath / "shaders/world/overlay/mc_depth_comp.spv").string());
        pipeline_ = gpu::ComputePipelineBuilder{}
                        .defineShader(shader)
                        .definePipelineLayout(resources.tables[0])
                        .build(framework.device());
    }
    return resources;
}

void McDepthWriter::write(const std::shared_ptr<Framework> &framework,
                          const std::shared_ptr<gpu::CommandBuffer> &commandBuffer,
                          const std::shared_ptr<gpu::DeviceLocalImage> &linearDepth,
                          const Target &target,
                          uint32_t width,
                          uint32_t height,
                          uint32_t frameIndex) {
    // Only the depth format Minecraft's main target uses: another would need copy rules of its own.
    if (target.image == VK_NULL_HANDLE || target.format != VK_FORMAT_D32_SFLOAT || linearDepth == nullptr) return;
    const uint32_t frameCount = framework->swapchain()->imageCount();
    if (frameIndex >= frameCount) return;
    SizedResources &resources = resourcesFor(*framework, width, height, frameCount);
    gpu::DeviceLocalImage &depth = *resources.depth;
    const std::shared_ptr<gpu::DescriptorTable> &table = resources.tables[frameIndex];
    const VkCommandBuffer commands = commandBuffer->vkCommandBuffer();

    const std::array<VkImageMemoryBarrier2, 2> barriers = {
        toGeneral(*linearDepth, VK_ACCESS_2_SHADER_READ_BIT),
        toGeneral(depth, VK_ACCESS_2_SHADER_WRITE_BIT),
    };
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependency.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commands, &dependency);
    // Both are used outside the layout tracking from here on (the copies below): their next tracked use waits on
    // everything before it.
    linearDepth->imageLayout() = VK_IMAGE_LAYOUT_GENERAL;
    depth.imageLayout() = VK_IMAGE_LAYOUT_GENERAL;

    table->bindImage(linearDepth, VK_IMAGE_LAYOUT_GENERAL, 0, 0);
    table->bindImage(resources.depth, VK_IMAGE_LAYOUT_GENERAL, 0, 1);
    const DepthConstants constants{
        .zScale = target.projection[0],
        .zOffset = target.projection[1],
        .wScale = target.projection[2],
        .wOffset = target.projection[3],
        .sourceWidth = linearDepth->width(),
        .sourceHeight = linearDepth->height(),
        .targetWidth = width,
        .targetHeight = height,
    };
    commandBuffer->bindDescriptorTable(table, VK_PIPELINE_BIND_POINT_COMPUTE)->bindComputePipeline(pipeline_);
    vkCmdPushConstants(commands, table->vkPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                       &constants);
    vkCmdDispatch(commands, (width + 15) / 16, (height + 15) / 16, 1);

    commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                 VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(commands, depth.vkImage(), VK_IMAGE_LAYOUT_GENERAL, resources.transfer->vkBuffer(), 1,
                           &region);

    commandBuffer->memoryBarrier(VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                 VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
    // Minecraft keeps its textures in the GENERAL layout all their life.
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vkCmdCopyBufferToImage(commands, resources.transfer->vkBuffer(), target.image, VK_IMAGE_LAYOUT_GENERAL, 1,
                           &region);

    // Ready for Minecraft's depth tests, and for whatever samples or copies its depth texture.
    commandBuffer->memoryBarrier(
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
}
