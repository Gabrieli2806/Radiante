#pragma once

#include "prism/all_extern.hpp"

#include <string>
#include <vector>

namespace gpu {
class VMA;
class Device;
class Buffer;
class CommandBuffer;

// Bytes per texel of the uncompressed formats the renderer uses; throws for any other.
size_t formatToByte(VkFormat format);

inline constexpr VkImageSubresourceRange wholeColorSubresourceRange = {
    VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
inline constexpr VkImageSubresourceRange wholeDepthSubresourceRange = {
    VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
inline constexpr VkImageSubresourceRange wholeStencilSubresourceRange = {
    VK_IMAGE_ASPECT_STENCIL_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
inline constexpr VkImageSubresourceRange wholeDepthStencilSubresourceRange = {
    VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, VK_REMAINING_MIP_LEVELS, 0,
    VK_REMAINING_ARRAY_LAYERS};
inline constexpr VkImageSubresourceLayers wholeColorSubresourceLayers = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
                                                                         VK_REMAINING_ARRAY_LAYERS};
inline constexpr VkImageSubresourceLayers wholeDepthSubresourceLayers = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0,
                                                                         VK_REMAINING_ARRAY_LAYERS};
inline constexpr VkImageSubresourceLayers wholeStencilSubresourceLayers = {VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0,
                                                                           VK_REMAINING_ARRAY_LAYERS};

// What an image was last used as, so its next use waits for exactly that (CommandBuffer::sync).
struct ImageState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
};

class Image {
  public:
    virtual ~Image() = default;
    virtual VkImageSubresourceRange subresources() const = 0;

    ImageState &state() {
        return state_;
    }
    // For code that transitions the image by hand and records the layout it left: the next tracked use then waits
    // on all earlier work, since what that code did is not known.
    VkImageLayout &imageLayout() {
        state_.stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        state_.access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        return state_.layout;
    }

    virtual uint32_t width() = 0;
    virtual uint32_t height() = 0;
    virtual uint32_t depth() = 0;
    virtual uint32_t layer() = 0;
    virtual VkFormat &vkFormat() = 0;
    virtual VkImage &vkImage() = 0;
    virtual VkImageView &vkImageView(int index = 0) = 0;

  protected:
    ImageState state_;
};

// An image the renderer did not make (Minecraft's swapchain or targets), viewed as a whole 2D color image.
class SwapchainImage : public Image, public SharedObject<SwapchainImage> {
  public:
    SwapchainImage(std::shared_ptr<Device> device, VkImage image, uint32_t width, uint32_t height, VkFormat format);
    ~SwapchainImage() override;

    uint32_t width() override {
        return width_;
    }
    uint32_t height() override {
        return height_;
    }
    uint32_t depth() override {
        return 1;
    }
    uint32_t layer() override {
        return 1;
    }
    VkFormat &vkFormat() override {
        return format_;
    }
    VkImage &vkImage() override {
        return image_;
    }
    VkImageView &vkImageView(int) override {
        return view_;
    }
    VkImageSubresourceRange subresources() const override {
        return {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }

  private:
    std::shared_ptr<Device> device_;
    uint32_t width_;
    uint32_t height_;
    VkFormat format_;
    VkImage image_;
    VkImageView view_ = VK_NULL_HANDLE;
};

// An image owned by someone else (Minecraft). No view is made; usable for barriers and copies.
class ExternalImage : public Image, public SharedObject<ExternalImage> {
  public:
    ExternalImage(VkImage image, uint32_t width, uint32_t height, VkFormat format)
        : image_(image), width_(width), height_(height), format_(format) {}

    uint32_t width() override {
        return width_;
    }
    uint32_t height() override {
        return height_;
    }
    uint32_t depth() override {
        return 1;
    }
    uint32_t layer() override {
        return 1;
    }
    VkFormat &vkFormat() override {
        return format_;
    }
    VkImage &vkImage() override {
        return image_;
    }
    VkImageView &vkImageView(int) override {
        return view_;
    }
    VkImageSubresourceRange subresources() const override {
        return {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }

  private:
    VkImage image_;
    uint32_t width_;
    uint32_t height_;
    VkFormat format_;
    VkImageView view_ = VK_NULL_HANDLE;
};

// What an image is made as.
struct ImageDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;  // above 1: a 3D image (one layer)
    uint32_t layers = 1; // above 1: an array (2D)
    uint32_t mipLevels = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags usage = 0; // transfer source and destination are always added
    VkImageCreateFlags flags = 0;
    VmaAllocationCreateFlags allocationFlags = 0;
    VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    bool keepStaging = false; // a host copy kept for repeated uploads
};

// A GPU image with its memory, its default view (index 0) and any added views. It carries the layout it was last
// left in. Destruction is deferred until no frame in flight can use it (gpu::retire).
class DeviceLocalImage : public Image, public SharedObject<DeviceLocalImage> {
  public:
    explicit DeviceLocalImage(std::shared_ptr<Device> device, std::shared_ptr<VMA> vma, const ImageDesc &desc);

    DeviceLocalImage(std::shared_ptr<Device> device,
                     std::shared_ptr<VMA> vma,
                     uint32_t width,
                     uint32_t height,
                     uint32_t layer,
                     VkFormat format,
                     VkImageUsageFlags usage);
    DeviceLocalImage(std::shared_ptr<Device> device,
                     std::shared_ptr<VMA> vma,
                     uint32_t width,
                     uint32_t height,
                     uint32_t depth,
                     uint32_t layer,
                     VkFormat format,
                     VkImageUsageFlags usage);
    DeviceLocalImage(std::shared_ptr<Device> device,
                     std::shared_ptr<VMA> vma,
                     bool persistStaging,
                     uint32_t width,
                     uint32_t height,
                     uint32_t layer,
                     VkFormat format,
                     VkImageUsageFlags usage);
    DeviceLocalImage(std::shared_ptr<Device> device,
                     std::shared_ptr<VMA> vma,
                     bool persistStaging,
                     uint32_t width,
                     uint32_t height,
                     uint32_t depth,
                     uint32_t layer,
                     VkFormat format,
                     VkImageUsageFlags usage);
    DeviceLocalImage(std::shared_ptr<Device> device,
                     std::shared_ptr<VMA> vma,
                     bool persistStaging,
                     uint32_t width,
                     uint32_t height,
                     uint32_t layer,
                     VkFormat format,
                     VkImageUsageFlags usage,
                     VmaAllocationCreateFlags allocationFlags,
                     VmaMemoryUsage vmaUsage,
                     VkImageCreateFlags imageCreateFlags = 0);
    DeviceLocalImage(std::shared_ptr<Device> device,
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
                     VkImageCreateFlags imageCreateFlags = 0);
    DeviceLocalImage(std::shared_ptr<Device> device,
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
                     VkImageCreateFlags imageCreateFlags = 0);
    ~DeviceLocalImage() override;

    // Fills the host copy (made for this upload when none is kept) with the whole image's texels.
    void uploadToStagingBuffer(void *src);
    // Copies the host copy into mip 0 of every layer; the image must be in TRANSFER_DST_OPTIMAL.
    void uploadToImage(VkCommandBuffer cmdBuffer);
    void uploadToImage(std::shared_ptr<CommandBuffer> cmdBuffer);
    void uploadToImage(VkCommandBuffer cmdBuffer, std::shared_ptr<Buffer> buffer, std::vector<VkBufferImageCopy> &regions);
    void uploadToImage(std::shared_ptr<CommandBuffer> cmdBuffer,
                       std::shared_ptr<Buffer> buffer,
                       std::vector<VkBufferImageCopy> &regions);

    uint32_t width() override {
        return desc_.width;
    }
    uint32_t height() override {
        return desc_.height;
    }
    uint32_t depth() override {
        return desc_.depth;
    }
    uint32_t layer() override {
        return desc_.layers;
    }
    uint32_t mipLevels() const {
        return desc_.mipLevels;
    }
    VkFormat &vkFormat() override {
        return desc_.format;
    }
    VkBuffer &vkStagingBuffer() {
        return stagingBuffer_;
    }
    VkImage &vkImage() override {
        return image_;
    }
    VkImageView &vkImageView(int index = 0) override {
        return views_[index];
    }
    void *mappedPtr() {
        return stagingMapped_;
    }
    VkImageSubresourceRange fullSubresourceRange() const;
    VkImageSubresourceRange subresources() const override {
        return fullSubresourceRange();
    }

    void addImageView(VkImageViewCreateInfo info);

  private:
    VkImageAspectFlags aspect() const;
    size_t byteSize() const;
    void makeStaging();
    void retireStaging();

    std::shared_ptr<Device> device_;
    std::shared_ptr<VMA> vma_;
    ImageDesc desc_;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation_ = VK_NULL_HANDLE;
    void *stagingMapped_ = nullptr;
    std::vector<VkImageView> views_;
};

class Sampler : public SharedObject<Sampler> {
  public:
    explicit Sampler(std::shared_ptr<Device> device);
    Sampler(std::shared_ptr<Device> device,
            VkFilter samplingMode,
            VkSamplerMipmapMode mipmapMode,
            VkSamplerAddressMode addressMode);
    ~Sampler();

    VkSampler vkSamper() {
        return sampler_;
    }
    VkSamplerAddressMode vkAddressMode() {
        return addressMode_;
    }
    VkFilter vkSamplingMode() {
        return filter_;
    }
    VkSamplerMipmapMode vkMipmapMode() {
        return mipmapMode_;
    }

  private:
    std::shared_ptr<Device> device_;
    VkFilter filter_;
    VkSamplerMipmapMode mipmapMode_;
    VkSamplerAddressMode addressMode_;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

// RGBA8 texels of one or more same-sized image files, stacked as layers.
class ImageLoader : public SharedObject<ImageLoader> {
  public:
    ImageLoader(std::vector<std::string> imagePaths, uint32_t forceChannel, bool convertLinearToSrgb = true);

    uint32_t width() {
        return width_;
    }
    uint32_t height() {
        return height_;
    }
    uint32_t layer() {
        return layers_;
    }
    void *data() {
        return data_.data();
    }

  private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t layers_ = 0;
    std::vector<uint8_t> data_;
};
}; // namespace gpu
