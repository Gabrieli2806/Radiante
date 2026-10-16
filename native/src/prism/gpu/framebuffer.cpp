#include "prism/gpu/framebuffer.hpp"

#include "prism/gpu/device.hpp"
#include "prism/gpu/image.hpp"
#include "prism/gpu/render_pass.hpp"
#include "prism/gpu/retire.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace gpu {
namespace {

std::string sizeText(VkExtent2D extent) {
    return std::to_string(extent.width) + "x" + std::to_string(extent.height);
}

} // namespace

// ---- Framebuffer

Framebuffer::Framebuffer(std::shared_ptr<Device> device, VkFramebuffer framebuffer)
    : device_(std::move(device)), framebuffer_(framebuffer) {}

Framebuffer::~Framebuffer() {
    if (framebuffer_ == VK_NULL_HANDLE) return;
    retire::later([device = std::move(device_), framebuffer = framebuffer_] {
        vkDestroyFramebuffer(device->vkDevice(), framebuffer, nullptr);
    });
}

VkFramebuffer &Framebuffer::vkFramebuffer() {
    return framebuffer_;
}

// ---- FramebufferBuilder

FramebufferBuilder::FramebufferAttachmentsBuilder::FramebufferAttachmentsBuilder(FramebufferBuilder &parent)
    : parent_(parent) {}

FramebufferBuilder::FramebufferAttachmentsBuilder &
FramebufferBuilder::FramebufferAttachmentsBuilder::defineAttachment(std::shared_ptr<Image> image, int viewIndex) {
    const VkExtent2D extent{image->width(), image->height()};
    if (parent_.views_.empty()) {
        parent_.extent_ = extent;
    } else if (extent.width != parent_.extent_.width || extent.height != parent_.extent_.height) {
        throw std::invalid_argument("a framebuffer attachment of " + sizeText(extent) + " does not match the " +
                                    sizeText(parent_.extent_) + " of the first");
    }
    parent_.views_.push_back(image->vkImageView(viewIndex));
    return *this;
}

FramebufferBuilder &FramebufferBuilder::FramebufferAttachmentsBuilder::endAttachment() {
    return parent_;
}

FramebufferBuilder::FramebufferAttachmentsBuilder &FramebufferBuilder::beginAttachment() {
    return attachmentsBuilder_;
}

std::shared_ptr<Framebuffer> FramebufferBuilder::build(std::shared_ptr<Device> device,
                                                       std::shared_ptr<RenderPass> renderPass) const {
    if (views_.empty()) throw std::logic_error("a framebuffer sized by its attachments needs at least one");
    return create(std::move(device), renderPass->vkRenderPass(), views_, extent_);
}

std::shared_ptr<Framebuffer> FramebufferBuilder::build(std::shared_ptr<Device> device,
                                                       std::shared_ptr<RenderPass> renderPass,
                                                       uint32_t width,
                                                       uint32_t height) const {
    return create(std::move(device), renderPass->vkRenderPass(), {}, {width, height});
}

std::shared_ptr<Framebuffer> FramebufferBuilder::create(std::shared_ptr<Device> device,
                                                        VkRenderPass renderPass,
                                                        const std::vector<VkImageView> &views,
                                                        VkExtent2D extent) {
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = renderPass;
    info.attachmentCount = static_cast<uint32_t>(views.size());
    info.pAttachments = views.data();
    info.width = extent.width;
    info.height = extent.height;
    info.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    const VkResult result = vkCreateFramebuffer(device->vkDevice(), &info, nullptr, &framebuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("framebuffer could not be made (" + std::to_string(result) + ")");
    }
    return Framebuffer::create(std::move(device), framebuffer);
}

} // namespace gpu
