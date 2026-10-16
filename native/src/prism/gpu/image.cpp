#define STB_IMAGE_IMPLEMENTATION
#include "prism/gpu/image.hpp"

#include "prism/gpu/buffer.hpp"
#include "prism/gpu/command.hpp"
#include "prism/gpu/device.hpp"
#include "prism/gpu/retire.hpp"
#include "prism/gpu/vma.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace gpu {

size_t formatToByte(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8_SINT: return 1;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8_SINT:
        case VK_FORMAT_R16_UNORM:
        case VK_FORMAT_R16_SNORM:
        case VK_FORMAT_R16_UINT:
        case VK_FORMAT_R16_SINT:
        case VK_FORMAT_R16_SFLOAT: return 2;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_UINT:
        case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SNORM:
        case VK_FORMAT_B8G8R8A8_UINT:
        case VK_FORMAT_B8G8R8A8_SINT:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R16G16_UNORM:
        case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R16G16_UINT:
        case VK_FORMAT_R16G16_SINT:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_R32_SINT:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT: return 4;
        case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SNORM:
        case VK_FORMAT_R16G16B16A16_UINT:
        case VK_FORMAT_R16G16B16A16_SINT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32G32_UINT:
        case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R32G32_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32_UINT:
        case VK_FORMAT_R32G32B32_SINT:
        case VK_FORMAT_R32G32B32_SFLOAT: return 12;
        case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SINT:
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        default: throw std::runtime_error("texel size of format " + std::to_string(format) + " is not known");
    }
}

namespace {

VkImageView makeView(VkDevice device, const VkImageViewCreateInfo &info) {
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("image view could not be made");
    }
    return view;
}

} // namespace

// ---- SwapchainImage

SwapchainImage::SwapchainImage(
    std::shared_ptr<Device> device, VkImage image, uint32_t width, uint32_t height, VkFormat format)
    : device_(std::move(device)), width_(width), height_(height), format_(format), image_(image) {
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image_;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format_;
    info.subresourceRange = wholeColorSubresourceRange;
    view_ = makeView(device_->vkDevice(), info);
}

SwapchainImage::~SwapchainImage() {
    retire::later([device = device_, view = view_] { vkDestroyImageView(device->vkDevice(), view, nullptr); });
}

// ---- DeviceLocalImage

DeviceLocalImage::DeviceLocalImage(std::shared_ptr<Device> device, std::shared_ptr<VMA> vma, const ImageDesc &desc)
    : device_(std::move(device)), vma_(std::move(vma)), desc_(desc) {
    if (desc_.depth == 0 || desc_.layers == 0 || desc_.mipLevels == 0) {
        throw std::invalid_argument("image with no depth, layer or level");
    }
    if (desc_.depth > 1 && (desc_.layers != 1 || (desc_.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0)) {
        throw std::invalid_argument("3D images have one layer and are not cube compatible");
    }
    if (desc_.keepStaging) makeStaging();

    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = desc_.flags;
    info.imageType = desc_.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    info.format = desc_.format;
    info.extent = {desc_.width, desc_.height, desc_.depth};
    info.mipLevels = desc_.mipLevels;
    info.arrayLayers = desc_.layers;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | desc_.usage;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.flags = desc_.allocationFlags;
    allocInfo.usage = desc_.memoryUsage;
    if (vmaCreateImage(vma_->allocator(), &info, &allocInfo, &image_, &allocation_, nullptr) != VK_SUCCESS) {
        radiante::err() << "[gpu] image " << desc_.width << "x" << desc_.height << "x" << desc_.depth << " of format "
                        << desc_.format << " could not be allocated" << std::endl;
        throw std::runtime_error("gpu image allocation failed");
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image_;
    viewInfo.viewType = desc_.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D
                        : desc_.layers == 1 ? VK_IMAGE_VIEW_TYPE_2D
                                            : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = desc_.format;
    viewInfo.subresourceRange = fullSubresourceRange();
    views_.push_back(makeView(device_->vkDevice(), viewInfo));
}

namespace {

ImageDesc describe(uint32_t mipLevels,
                   uint32_t width,
                   uint32_t height,
                   uint32_t depth,
                   uint32_t layers,
                   VkFormat format,
                   VkImageUsageFlags usage,
                   bool keepStaging,
                   VmaAllocationCreateFlags allocationFlags,
                   VmaMemoryUsage memoryUsage,
                   VkImageCreateFlags flags) {
    ImageDesc desc;
    desc.width = width;
    desc.height = height;
    desc.depth = depth;
    desc.layers = layers;
    desc.mipLevels = mipLevels;
    desc.format = format;
    desc.usage = usage;
    desc.flags = flags;
    desc.allocationFlags = allocationFlags;
    desc.memoryUsage = memoryUsage;
    desc.keepStaging = keepStaging;
    return desc;
}

constexpr VmaMemoryUsage kDeviceMemory = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

} // namespace

DeviceLocalImage::DeviceLocalImage(std::shared_ptr<Device> device,
                                   std::shared_ptr<VMA> vma,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t layer,
                                   VkFormat format,
                                   VkImageUsageFlags usage)
    : DeviceLocalImage(std::move(device),
                       std::move(vma),
                       describe(1, width, height, 1, layer, format, usage, false, 0, kDeviceMemory, 0)) {}

DeviceLocalImage::DeviceLocalImage(std::shared_ptr<Device> device,
                                   std::shared_ptr<VMA> vma,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t depth,
                                   uint32_t layer,
                                   VkFormat format,
                                   VkImageUsageFlags usage)
    : DeviceLocalImage(std::move(device),
                       std::move(vma),
                       describe(1, width, height, depth, layer, format, usage, false, 0, kDeviceMemory, 0)) {}

DeviceLocalImage::DeviceLocalImage(std::shared_ptr<Device> device,
                                   std::shared_ptr<VMA> vma,
                                   bool persistStaging,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t layer,
                                   VkFormat format,
                                   VkImageUsageFlags usage)
    : DeviceLocalImage(std::move(device),
                       std::move(vma),
                       describe(1, width, height, 1, layer, format, usage, persistStaging, 0, kDeviceMemory, 0)) {}

DeviceLocalImage::DeviceLocalImage(std::shared_ptr<Device> device,
                                   std::shared_ptr<VMA> vma,
                                   bool persistStaging,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t depth,
                                   uint32_t layer,
                                   VkFormat format,
                                   VkImageUsageFlags usage)
    : DeviceLocalImage(
          std::move(device),
          std::move(vma),
          describe(1, width, height, depth, layer, format, usage, persistStaging, 0, kDeviceMemory, 0)) {}

DeviceLocalImage::DeviceLocalImage(std::shared_ptr<Device> device,
                                   std::shared_ptr<VMA> vma,
                                   bool persistStaging,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t layer,
                                   VkFormat format,
                                   VkImageUsageFlags usage,
                                   VmaAllocationCreateFlags allocationFlags,
                                   VmaMemoryUsage vmaUsage,
                                   VkImageCreateFlags imageCreateFlags)
    : DeviceLocalImage(std::move(device),
                       std::move(vma),
                       describe(1, width, height, 1, layer, format, usage, persistStaging, allocationFlags, vmaUsage,
                                imageCreateFlags)) {}

DeviceLocalImage::DeviceLocalImage(std::shared_ptr<Device> device,
                                   std::shared_ptr<VMA> vma,
                                   bool persistStaging,
                                   uint32_t mipLevels,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t layer,
                                   VkFormat format,
                                   VkImageUsageFlags usage,
                                   VmaAllocationCreateFlags allocationFlag,
                                   VmaMemoryUsage vmaUsage,
                                   VkImageCreateFlags imageCreateFlags)
    : DeviceLocalImage(std::move(device),
                       std::move(vma),
                       describe(mipLevels, width, height, 1, layer, format, usage, persistStaging, allocationFlag,
                                vmaUsage, imageCreateFlags)) {}

DeviceLocalImage::DeviceLocalImage(std::shared_ptr<Device> device,
                                   std::shared_ptr<VMA> vma,
                                   bool persistStaging,
                                   uint32_t mipLevels,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t depth,
                                   uint32_t layer,
                                   VkFormat format,
                                   VkImageUsageFlags usage,
                                   VmaAllocationCreateFlags allocationFlag,
                                   VmaMemoryUsage vmaUsage,
                                   VkImageCreateFlags imageCreateFlags)
    : DeviceLocalImage(std::move(device),
                       std::move(vma),
                       describe(mipLevels, width, height, depth, layer, format, usage, persistStaging, allocationFlag,
                                vmaUsage, imageCreateFlags)) {}

DeviceLocalImage::~DeviceLocalImage() {
    retireStaging();
    retire::later([device = device_, vma = vma_, image = image_, allocation = allocation_, views = views_] {
        for (VkImageView view : views) vkDestroyImageView(device->vkDevice(), view, nullptr);
        vmaDestroyImage(vma->allocator(), image, allocation);
    });
}

VkImageAspectFlags DeviceLocalImage::aspect() const {
    return (desc_.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0 ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                            : VK_IMAGE_ASPECT_COLOR_BIT;
}

size_t DeviceLocalImage::byteSize() const {
    return static_cast<size_t>(desc_.width) * desc_.height * desc_.depth * desc_.layers * formatToByte(desc_.format);
}

VkImageSubresourceRange DeviceLocalImage::fullSubresourceRange() const {
    return {aspect(), 0, desc_.mipLevels, 0, desc_.depth > 1 ? 1u : desc_.layers};
}

void DeviceLocalImage::makeStaging() {
    if (stagingBuffer_ != VK_NULL_HANDLE) return;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = byteSize();
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo result{};
    if (vmaCreateBuffer(vma_->allocator(), &info, &allocInfo, &stagingBuffer_, &stagingAllocation_, &result) !=
        VK_SUCCESS) {
        throw std::runtime_error("image staging buffer could not be allocated");
    }
    stagingMapped_ = result.pMappedData;
}

void DeviceLocalImage::retireStaging() {
    if (stagingBuffer_ == VK_NULL_HANDLE) return;
    retire::later([vma = vma_, buffer = stagingBuffer_, allocation = stagingAllocation_] {
        vmaDestroyBuffer(vma->allocator(), buffer, allocation);
    });
    stagingBuffer_ = VK_NULL_HANDLE;
    stagingAllocation_ = VK_NULL_HANDLE;
    stagingMapped_ = nullptr;
}

void DeviceLocalImage::uploadToStagingBuffer(void *src) {
    makeStaging();
    size_t size = byteSize();
    std::memcpy(stagingMapped_, src, size);
    vmaFlushAllocation(vma_->allocator(), stagingAllocation_, 0, size);
}

void DeviceLocalImage::uploadToImage(VkCommandBuffer cmdBuffer) {
    if (stagingBuffer_ == VK_NULL_HANDLE) return;
    VkBufferImageCopy region{};
    region.imageSubresource = {aspect(), 0, 0, desc_.depth > 1 ? 1u : desc_.layers};
    region.imageExtent = {desc_.width, desc_.height, desc_.depth};
    vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer_, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    if (!desc_.keepStaging) retireStaging();
}

void DeviceLocalImage::uploadToImage(std::shared_ptr<CommandBuffer> cmdBuffer) {
    uploadToImage(cmdBuffer->vkCommandBuffer());
}

void DeviceLocalImage::uploadToImage(VkCommandBuffer cmdBuffer,
                                     std::shared_ptr<Buffer> buffer,
                                     std::vector<VkBufferImageCopy> &regions) {
    vkCmdCopyBufferToImage(cmdBuffer, buffer->vkBuffer(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());
}

void DeviceLocalImage::uploadToImage(std::shared_ptr<CommandBuffer> cmdBuffer,
                                     std::shared_ptr<Buffer> buffer,
                                     std::vector<VkBufferImageCopy> &regions) {
    uploadToImage(cmdBuffer->vkCommandBuffer(), std::move(buffer), regions);
}

void DeviceLocalImage::addImageView(VkImageViewCreateInfo info) {
    views_.push_back(makeView(device_->vkDevice(), info));
}

// ---- Sampler

Sampler::Sampler(std::shared_ptr<Device> device)
    : Sampler(std::move(device), VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT) {}

Sampler::Sampler(std::shared_ptr<Device> device,
                 VkFilter samplingMode,
                 VkSamplerMipmapMode mipmapMode,
                 VkSamplerAddressMode addressMode)
    : device_(std::move(device)), filter_(samplingMode), mipmapMode_(mipmapMode), addressMode_(addressMode) {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = samplingMode;
    info.minFilter = samplingMode;
    info.mipmapMode = mipmapMode;
    info.addressModeU = addressMode;
    info.addressModeV = addressMode;
    info.addressModeW = addressMode;
    info.maxAnisotropy = 16.0f;
    info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    info.compareOp = VK_COMPARE_OP_ALWAYS;
    info.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(device_->vkDevice(), &info, nullptr, &sampler_) != VK_SUCCESS) {
        throw std::runtime_error("sampler could not be made");
    }
}

Sampler::~Sampler() {
    retire::later([device = device_, sampler = sampler_] { vkDestroySampler(device->vkDevice(), sampler, nullptr); });
}

// ---- ImageLoader

namespace {

uint8_t encodeSrgb(uint8_t linear) {
    float v = linear / 255.0f;
    float s = v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::clamp(s, 0.0f, 1.0f) * 255.0f);
}

} // namespace

ImageLoader::ImageLoader(std::vector<std::string> imagePaths, uint32_t forceChannel, bool convertLinearToSrgb)
    : channel_(forceChannel), layers_(static_cast<uint32_t>(imagePaths.size())) {
    if (imagePaths.empty()) throw std::invalid_argument("no image to load");
    if (forceChannel != 4) throw std::invalid_argument("images are loaded as RGBA only");

    for (size_t i = 0; i < imagePaths.size(); i++) {
        int width = 0, height = 0, channels = 0;
        stbi_uc *pixels = stbi_load(imagePaths[i].c_str(), &width, &height, &channels, 0);
        if (pixels == nullptr) throw std::runtime_error("image could not be read: " + imagePaths[i]);
        if (i == 0) {
            width_ = static_cast<uint32_t>(width);
            height_ = static_cast<uint32_t>(height);
            data_.reserve(static_cast<size_t>(width_) * height_ * 4 * layers_);
        } else if (static_cast<uint32_t>(width) != width_ || static_cast<uint32_t>(height) != height_) {
            stbi_image_free(pixels);
            throw std::runtime_error("layered images differ in size: " + imagePaths[i]);
        }
        if (channels != 1 && channels != 3 && channels != 4) {
            stbi_image_free(pixels);
            throw std::runtime_error("unsupported channel count in " + imagePaths[i]);
        }
        auto colour = [&](uint8_t v) { return convertLinearToSrgb ? encodeSrgb(v) : v; };
        for (size_t p = 0, n = static_cast<size_t>(width) * height; p < n; p++) {
            const stbi_uc *texel = pixels + p * channels;
            if (channels == 1) {
                uint8_t v = colour(texel[0]);
                data_.insert(data_.end(), {v, v, v, 255});
            } else {
                data_.insert(data_.end(), {colour(texel[0]), colour(texel[1]), colour(texel[2]),
                                           channels == 4 ? texel[3] : uint8_t(255)});
            }
        }
        stbi_image_free(pixels);
    }
}

} // namespace gpu
