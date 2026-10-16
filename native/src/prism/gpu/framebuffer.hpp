#pragma once

#include "prism/all_extern.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace gpu {
class Image;
class RenderPass;
class Device;

// Owns a VkFramebuffer. Frames still in flight may use it, so it is destroyed through gpu::retire.
class Framebuffer : public SharedObject<Framebuffer> {
  public:
    Framebuffer(std::shared_ptr<Device> device, VkFramebuffer framebuffer);
    ~Framebuffer();
    Framebuffer(const Framebuffer &) = delete;
    Framebuffer &operator=(const Framebuffer &) = delete;

    VkFramebuffer &vkFramebuffer();

  private:
    std::shared_ptr<Device> device_;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
};

// The begin...() scope hands out a part of this builder, so it is neither copied nor moved.
class FramebufferBuilder {
  public:
    class FramebufferAttachmentsBuilder {
      public:
        explicit FramebufferAttachmentsBuilder(FramebufferBuilder &parent);

        // The image's view viewIndex (see Image::vkImageView), as the next attachment. Every attachment must be as
        // large as the first; a different size throws.
        FramebufferAttachmentsBuilder &defineAttachment(std::shared_ptr<Image> image, int viewIndex = 0);
        FramebufferBuilder &endAttachment();

      private:
        FramebufferBuilder &parent_;
    };

    FramebufferBuilder() = default;
    FramebufferBuilder(const FramebufferBuilder &) = delete;
    FramebufferBuilder &operator=(const FramebufferBuilder &) = delete;

    FramebufferAttachmentsBuilder &beginAttachment();
    // A framebuffer of the defined attachments, one layer, as large as they are. Throws without attachments.
    std::shared_ptr<Framebuffer> build(std::shared_ptr<Device> device, std::shared_ptr<RenderPass> renderPass) const;
    // A framebuffer of the given size without any attachment (whatever was defined is left out), for a render pass
    // that has none.
    std::shared_ptr<Framebuffer> build(std::shared_ptr<Device> device,
                                       std::shared_ptr<RenderPass> renderPass,
                                       uint32_t width,
                                       uint32_t height) const;

  private:
    static std::shared_ptr<Framebuffer> create(std::shared_ptr<Device> device,
                                               VkRenderPass renderPass,
                                               const std::vector<VkImageView> &views,
                                               VkExtent2D extent);

    FramebufferAttachmentsBuilder attachmentsBuilder_{*this};

    std::vector<VkImageView> views_;
    VkExtent2D extent_{};
};

} // namespace gpu
