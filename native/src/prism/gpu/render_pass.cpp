#include "prism/gpu/render_pass.hpp"

#include "prism/gpu/device.hpp"
#include "prism/gpu/retire.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace gpu {

// ---- RenderPass

RenderPass::RenderPass(std::shared_ptr<Device> device, VkRenderPass renderPass)
    : device_(std::move(device)), renderPass_(renderPass) {}

RenderPass::~RenderPass() {
    if (renderPass_ == VK_NULL_HANDLE) return;
    retire::later([device = std::move(device_), renderPass = renderPass_] {
        vkDestroyRenderPass(device->vkDevice(), renderPass, nullptr);
    });
}

VkRenderPass &RenderPass::vkRenderPass() {
    return renderPass_;
}

// ---- RenderPassBuilder scopes

RenderPassBuilder::AttachmentDescriptionBuilder::AttachmentDescriptionBuilder(RenderPassBuilder &parent)
    : parent_(parent) {}

RenderPassBuilder::AttachmentDescriptionBuilder &
RenderPassBuilder::AttachmentDescriptionBuilder::defineAttachmentDescription(
    VkAttachmentDescription attachmentDescription) {
    parent_.attachments_.push_back(attachmentDescription);
    return *this;
}

RenderPassBuilder &RenderPassBuilder::AttachmentDescriptionBuilder::endAttachmentDescription() {
    return parent_;
}

RenderPassBuilder::AttachmentReferenceBuilder::AttachmentReferenceBuilder(RenderPassBuilder &parent)
    : parent_(parent) {}

RenderPassBuilder::AttachmentReferenceBuilder &
RenderPassBuilder::AttachmentReferenceBuilder::defineAttachmentReference(VkAttachmentReference attachmentReference) {
    parent_.references_.push_back(attachmentReference);
    return *this;
}

RenderPassBuilder &RenderPassBuilder::AttachmentReferenceBuilder::endAttachmentReference() {
    return parent_;
}

RenderPassBuilder::SubpassDescriptionBuilder::SubpassDescriptionBuilder(RenderPassBuilder &parent) : parent_(parent) {}

RenderPassBuilder::SubpassDescriptionBuilder &
RenderPassBuilder::SubpassDescriptionBuilder::defineSubpassDescription(SubpassDescription subpassDescription) {
    parent_.subpasses_.push_back(std::move(subpassDescription));
    return *this;
}

RenderPassBuilder &RenderPassBuilder::SubpassDescriptionBuilder::endSubpassDescription() {
    return parent_;
}

RenderPassBuilder::SubpassDependencyBuilder::SubpassDependencyBuilder(RenderPassBuilder &parent) : parent_(parent) {}

RenderPassBuilder::SubpassDependencyBuilder &
RenderPassBuilder::SubpassDependencyBuilder::defineSubpassDependency(VkSubpassDependency subpassDependency) {
    parent_.dependencies_.push_back(subpassDependency);
    return *this;
}

RenderPassBuilder &RenderPassBuilder::SubpassDependencyBuilder::endSubpassDependency() {
    return parent_;
}

// ---- RenderPassBuilder

RenderPassBuilder::AttachmentDescriptionBuilder &RenderPassBuilder::beginAttachmentDescription() {
    return attachmentDescriptionBuilder_;
}

RenderPassBuilder::AttachmentReferenceBuilder &RenderPassBuilder::beginAttachmentReference() {
    return attachmentReferenceBuilder_;
}

RenderPassBuilder::SubpassDescriptionBuilder &RenderPassBuilder::beginSubpassDescription() {
    return subpassDescriptionBuilder_;
}

RenderPassBuilder::SubpassDependencyBuilder &RenderPassBuilder::beginSubpassDependency() {
    return subpassDependencyBuilder_;
}

const VkAttachmentReference *RenderPassBuilder::references(uint32_t first, size_t count) const {
    if (first >= references_.size() || count > references_.size() - first) {
        const std::string last = count > 1 ? "-" + std::to_string(first + count - 1) : "";
        throw std::out_of_range("a render pass subpass refers to attachment reference " + std::to_string(first) +
                                last + ", but " + std::to_string(references_.size()) + " are defined");
    }
    return references_.data() + first;
}

std::shared_ptr<RenderPass> RenderPassBuilder::build(std::shared_ptr<Device> device) const {
    // The descriptions point into these per-subpass lists, so they are all filled before any is read.
    std::vector<std::vector<VkAttachmentReference>> inputs(subpasses_.size());
    std::vector<std::vector<VkAttachmentReference>> colors(subpasses_.size());
    std::vector<VkSubpassDescription> descriptions(subpasses_.size());
    for (size_t i = 0; i < subpasses_.size(); i++) {
        const SubpassDescription &subpass = subpasses_[i];
        for (uint32_t index : subpass.inputAttachmentIndices) inputs[i].push_back(*references(index, 1));
        for (uint32_t index : subpass.colorAttachmentIndices) colors[i].push_back(*references(index, 1));

        VkSubpassDescription &description = descriptions[i];
        description.flags = subpass.flags;
        description.pipelineBindPoint = subpass.pipelineBindPoint;
        description.inputAttachmentCount = static_cast<uint32_t>(inputs[i].size());
        description.pInputAttachments = inputs[i].data();
        description.colorAttachmentCount = static_cast<uint32_t>(colors[i].size());
        description.pColorAttachments = colors[i].data();
        if (subpass.resolveAttachmentIndex != noAttachment) {
            description.pResolveAttachments = references(subpass.resolveAttachmentIndex, colors[i].size());
        }
        if (subpass.depthStencilAttachmentIndex != noAttachment) {
            description.pDepthStencilAttachment = references(subpass.depthStencilAttachmentIndex, 1);
        }
        description.preserveAttachmentCount = static_cast<uint32_t>(subpass.preserveAttachmentIndices.size());
        description.pPreserveAttachments = subpass.preserveAttachmentIndices.data();
    }

    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = static_cast<uint32_t>(attachments_.size());
    info.pAttachments = attachments_.data();
    info.subpassCount = static_cast<uint32_t>(descriptions.size());
    info.pSubpasses = descriptions.data();
    info.dependencyCount = static_cast<uint32_t>(dependencies_.size());
    info.pDependencies = dependencies_.data();

    VkRenderPass renderPass = VK_NULL_HANDLE;
    const VkResult result = vkCreateRenderPass(device->vkDevice(), &info, nullptr, &renderPass);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("render pass could not be made (" + std::to_string(result) + ")");
    }
    return RenderPass::create(std::move(device), renderPass);
}

} // namespace gpu
