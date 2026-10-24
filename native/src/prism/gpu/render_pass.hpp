#pragma once

#include "prism/all_extern.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace gpu {
class Device;

// Owns a VkRenderPass. Frames still in flight may use it, so it is destroyed through gpu::retire.
class RenderPass : public SharedObject<RenderPass> {
  public:
    RenderPass(std::shared_ptr<Device> device, VkRenderPass renderPass);
    ~RenderPass();
    RenderPass(const RenderPass &) = delete;
    RenderPass &operator=(const RenderPass &) = delete;

    VkRenderPass &vkRenderPass();

  private:
    std::shared_ptr<Device> device_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
};

// A render pass from attachment descriptions, a list of attachment references and subpasses that pick from that list
// by index. Indices are checked when the pass is built.
//
// The begin...() scopes hand out parts of this builder, so it is neither copied nor moved.
class RenderPassBuilder {
  public:
    // For SubpassDescription: the subpass has no such attachment.
    static constexpr uint32_t noAttachment = static_cast<uint32_t>(-1);

    class AttachmentDescriptionBuilder {
      public:
        explicit AttachmentDescriptionBuilder(RenderPassBuilder &parent);

        AttachmentDescriptionBuilder &defineAttachmentDescription(VkAttachmentDescription attachmentDescription);
        RenderPassBuilder &endAttachmentDescription();

      private:
        RenderPassBuilder &parent_;
    };

    class AttachmentReferenceBuilder {
      public:
        explicit AttachmentReferenceBuilder(RenderPassBuilder &parent);

        AttachmentReferenceBuilder &defineAttachmentReference(VkAttachmentReference attachmentReference);
        RenderPassBuilder &endAttachmentReference();

      private:
        RenderPassBuilder &parent_;
    };

    // A subpass, its attachments given as indices into the attachment references.
    struct SubpassDescription {
        VkSubpassDescriptionFlags flags{};
        VkPipelineBindPoint pipelineBindPoint{};
        std::vector<uint32_t> inputAttachmentIndices;
        std::vector<uint32_t> colorAttachmentIndices;
        // The first of one resolve reference per colour attachment, which follow it in the reference list.
        uint32_t resolveAttachmentIndex = noAttachment;
        uint32_t depthStencilAttachmentIndex = noAttachment;
        // Attachment numbers (not references) whose contents the subpass keeps.
        std::vector<uint32_t> preserveAttachmentIndices;
    };

    class SubpassDescriptionBuilder {
      public:
        explicit SubpassDescriptionBuilder(RenderPassBuilder &parent);

        SubpassDescriptionBuilder &defineSubpassDescription(SubpassDescription subpassDescription);
        RenderPassBuilder &endSubpassDescription();

      private:
        RenderPassBuilder &parent_;
    };

    RenderPassBuilder() = default;
    RenderPassBuilder(const RenderPassBuilder &) = delete;
    RenderPassBuilder &operator=(const RenderPassBuilder &) = delete;

    AttachmentDescriptionBuilder &beginAttachmentDescription();
    AttachmentReferenceBuilder &beginAttachmentReference();
    SubpassDescriptionBuilder &beginSubpassDescription();

    // Throws when a subpass names a reference that was not defined, or when the driver cannot make the pass.
    std::shared_ptr<RenderPass> build(std::shared_ptr<Device> device) const;

  private:
    const VkAttachmentReference *references(uint32_t first, size_t count) const;

    AttachmentDescriptionBuilder attachmentDescriptionBuilder_{*this};
    AttachmentReferenceBuilder attachmentReferenceBuilder_{*this};
    SubpassDescriptionBuilder subpassDescriptionBuilder_{*this};

    std::vector<VkAttachmentDescription> attachments_;
    std::vector<VkAttachmentReference> references_;
    std::vector<SubpassDescription> subpasses_;
};

} // namespace gpu
