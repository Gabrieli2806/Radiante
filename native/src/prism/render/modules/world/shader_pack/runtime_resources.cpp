#include "prism/render/modules/world/shader_pack/shader_pack_internal.hpp"

#include "prism/render/render_framework.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace {

using Dimension = ShaderPackLoader::TextureDimension;

bool isLayered(Dimension dimension) {
    return dimension == Dimension::Texture3D || dimension == Dimension::Texture2DArray;
}

// A positive extent from a size expression, rounded up to whole texels.
uint32_t wholeTexels(double extent) {
    return std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(extent)));
}

VkImageViewCreateInfo layerView(gpu::DeviceLocalImage &image, VkImageViewType type, uint32_t layer, uint32_t count) {
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.vkImage();
    view.viewType = type;
    view.format = image.vkFormat();
    view.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY};
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, count};
    return view;
}

std::shared_ptr<gpu::DeviceLocalBuffer>
makeRuntimeBuffer(const std::shared_ptr<gpu::Device> &device, const std::shared_ptr<gpu::VMA> &vma, size_t size) {
    return gpu::DeviceLocalBuffer::create(vma, device, false, size,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0,
                                          VMA_MEMORY_USAGE_GPU_ONLY);
}

std::vector<uint8_t> readTextureFile(const fs::path &path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open()) throw std::runtime_error("failed to open imported texture: " + path.string());
    const std::streamsize size = stream.tellg();
    if (size < 0) throw std::runtime_error("failed to read imported texture size: " + path.string());
    stream.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0 && !stream.read(reinterpret_cast<char *>(data.data()), size)) {
        throw std::runtime_error("failed to read imported texture data: " + path.string());
    }
    return data;
}

// Records `record` into a one-off command buffer on the main queue, then waits until it and everything submitted
// before it have run. A fence rather than waiting for the queue to go idle, which would hold the queue (Minecraft
// submits to it too) for as long as the work runs.
template <typename Record>
void submitAndWait(Framework &framework, Record &&record) {
    const auto device = framework.device();
    const auto commandPool = gpu::CommandPool::create(framework.physicalDevice(), device);
    const auto commandBuffer = gpu::CommandBuffer::create(device, commandPool);
    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    record(*commandBuffer);
    commandBuffer->end();

    const auto fence = gpu::Fence::create(device);
    commandBuffer->submitMainQueueIndividual(device, fence);
    if (vkWaitForFences(device->vkDevice(), 1, &fence->vkFence(), VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        throw std::runtime_error("the shader pack's textures and buffers could not be prepared on the GPU");
    }
}

} // namespace

void ShaderPack::ensureRuntimeResources(uint32_t referenceWidth, uint32_t referenceHeight) {
    if (runtimeResourcesReady_ && referenceWidth_ == referenceWidth && referenceHeight_ == referenceHeight) {
        refreshRuntimeBuffers();
        return;
    }

    // The old resources go with the frames that may still use them (they retire on destruction).
    runtimeResourcesReady_ = false;
    runtimeTextures_.clear();
    runtimeTextureIndices_.clear();
    runtimeBuffers_.clear();
    runtimeBufferIndices_.clear();
    referenceWidth_ = referenceWidth;
    referenceHeight_ = referenceHeight;

    initRuntimeTextures();
    initRuntimeBuffers();
    loadImportedTextures();
    prepareRuntimeResources();
    runtimeResourcesReady_ = true;
}

void ShaderPack::refreshRuntimeBuffers() {
    if (!runtimeResourcesReady_ || runtimeBuffers_.empty()) return;

    std::shared_ptr<Framework> framework;
    for (auto &buffer : runtimeBuffers_) {
        const size_t size = runtimeBufferSize(buffer.config);
        const bool fits = !buffer.frameBuffers.empty() &&
                          std::all_of(buffer.frameBuffers.begin(), buffer.frameBuffers.end(), [&](const auto &frame) {
                              return frame != nullptr && frame->size() == size;
                          });
        if (fits) continue;

        // Remade at the new size, not cleared. The old ones go with the frames that may still use them.
        if (framework == nullptr) framework = framework_.lock();
        buffer.frameBuffers.assign(buffer.config.shared ? 1 : framework->swapchain()->imageCount(), nullptr);
        for (auto &frame : buffer.frameBuffers) frame = makeRuntimeBuffer(framework->device(), framework->vma(), size);
    }
}

void ShaderPack::setRuntimeResourceExpressionVariables(const std::vector<ExpressionEvaluator::Variable> &variables) {
    // Copy-assigned: the vector keeps its storage from frame to frame.
    runtimeResourceExpressionVariables_ = variables;
}

void ShaderPack::bindRuntimeResources(const std::shared_ptr<gpu::DescriptorTable> &descriptorTable,
                                      uint32_t setIndex,
                                      uint32_t frameIndex) {
    if (!runtimeResourcesReady_ || descriptorTable == nullptr) return;

    for (const auto &texture : runtimeTextures_) {
        const auto &config = texture.config;
        if (config.sampledBinding.has_value()) {
            // A texture the passes also write stays in GENERAL; the others are sampled read-only.
            const VkImageLayout layout = config.imported || !config.storageBinding.has_value()
                                             ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                             : VK_IMAGE_LAYOUT_GENERAL;
            descriptorTable->bindSamplerImage(texture.sampler, findRuntimeVKTexture(texture, frameIndex), layout,
                                              setIndex, *config.sampledBinding, 0, texture.sampledViewIndex);
        }
        if (config.storageBinding.has_value() && !config.imported) {
            descriptorTable->bindImage(findRuntimeVKTexture(texture, frameIndex), VK_IMAGE_LAYOUT_GENERAL, setIndex,
                                       *config.storageBinding);
        }
    }
    for (const auto &buffer : runtimeBuffers_) {
        descriptorTable->bindBuffer(findRuntimeVKBuffer(buffer, frameIndex), setIndex, buffer.config.binding);
    }
}

void ShaderPack::defineRuntimeResourceDescriptorSet(gpu::DescriptorTableBuilder &builder,
                                                    VkShaderStageFlags sampledImageStageFlags,
                                                    VkShaderStageFlags storageImageStageFlags,
                                                    VkShaderStageFlags storageBufferStageFlags) const {
    if (!hasRuntimeResources()) return;

    auto &set = builder.beginDescriptorLayoutSet();
    auto &bindings = set.beginDescriptorLayoutSetBinding();
    const auto define = [&](uint32_t binding, VkDescriptorType type, VkShaderStageFlags stages) {
        bindings.defineDescriptorLayoutSetBinding({
            .binding = binding,
            .descriptorType = type,
            .descriptorCount = 1,
            .stageFlags = stages,
        });
    };
    for (const auto &texture : shaderPack_.textures) {
        if (texture.sampledBinding.has_value()) {
            define(*texture.sampledBinding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampledImageStageFlags);
        }
        if (texture.storageBinding.has_value()) {
            define(*texture.storageBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storageImageStageFlags);
        }
    }
    for (const auto &buffer : shaderPack_.buffers) {
        define(buffer.binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBufferStageFlags);
    }
    bindings.endDescriptorLayoutSetBinding();
    set.endDescriptorLayoutSet();
}

uint32_t ShaderPack::executionSet(uint32_t runtimeResourceSet) const {
    return runtimeResourceSet + (hasRuntimeResources() ? 1u : 0u);
}

bool ShaderPack::hasRuntimeResources() const {
    return !shaderPack_.textures.empty() || !shaderPack_.buffers.empty();
}

bool ShaderPack::runtimeResourcesReady() const {
    return runtimeResourcesReady_;
}

void ShaderPack::preClose() {
    runtimeTextures_.clear();
    runtimeTextureIndices_.clear();
    runtimeBuffers_.clear();
    runtimeBufferIndices_.clear();
    runtimeResourcesReady_ = false;
    referenceWidth_ = 0;
    referenceHeight_ = 0;
}

std::optional<std::reference_wrapper<ShaderPack::RuntimeTexture>>
ShaderPack::findRuntimeTexture(std::string_view name) {
    const auto found = runtimeTextureIndices_.find(name);
    if (found == runtimeTextureIndices_.end()) return std::nullopt;
    return runtimeTextures_[found->second];
}

std::optional<std::reference_wrapper<const ShaderPack::RuntimeTexture>>
ShaderPack::findRuntimeTexture(std::string_view name) const {
    const auto found = runtimeTextureIndices_.find(name);
    if (found == runtimeTextureIndices_.end()) return std::nullopt;
    return runtimeTextures_[found->second];
}

std::optional<std::reference_wrapper<ShaderPack::RuntimeBuffer>>
ShaderPack::findRuntimeBuffer(std::string_view name) {
    const auto found = runtimeBufferIndices_.find(name);
    if (found == runtimeBufferIndices_.end()) return std::nullopt;
    return runtimeBuffers_[found->second];
}

std::optional<std::reference_wrapper<const ShaderPack::RuntimeBuffer>>
ShaderPack::findRuntimeBuffer(std::string_view name) const {
    const auto found = runtimeBufferIndices_.find(name);
    if (found == runtimeBufferIndices_.end()) return std::nullopt;
    return runtimeBuffers_[found->second];
}

std::shared_ptr<gpu::DeviceLocalImage> ShaderPack::findRuntimeVKTexture(RuntimeTexture &runtimeTexture,
                                                                       uint32_t frameIndex) {
    return std::as_const(*this).findRuntimeVKTexture(std::as_const(runtimeTexture), frameIndex);
}

std::shared_ptr<gpu::DeviceLocalImage> ShaderPack::findRuntimeVKTexture(const RuntimeTexture &runtimeTexture,
                                                                       uint32_t frameIndex) const {
    if (runtimeTexture.config.imported) return runtimeTexture.importedImage;
    return runtimeTexture.frameImages[runtimeTexture.config.shared ? 0 : frameIndex];
}

uint32_t ShaderPack::runtimeTextureSingleLayerViewIndex(const RuntimeTexture &runtimeTexture, uint32_t layer) const {
    const auto &config = runtimeTexture.config;
    if (runtimeTexture.singleLayerViewBaseIndex == 0) {
        throw std::runtime_error("runtime texture does not expose per-layer views: " + config.name);
    }
    if (config.dimension == Dimension::Cube) {
        if (layer >= 6) throw std::runtime_error("cube face index out of range: " + config.name);
        return runtimeTexture.singleLayerViewBaseIndex + layer;
    }
    if (config.dimension != Dimension::Texture2DArray) {
        throw std::runtime_error("runtime texture is not a 2d_array: " + config.name);
    }

    uint32_t layerCount = 0;
    if (config.imported) {
        layerCount = config.importedDepth;
    } else if (!runtimeTexture.frameImages.empty() && runtimeTexture.frameImages[0] != nullptr) {
        layerCount = runtimeTexture.frameImages[0]->layer();
    }
    if (layer >= layerCount) throw std::runtime_error("2d_array layer index out of range: " + config.name);
    return runtimeTexture.singleLayerViewBaseIndex + layer;
}

std::shared_ptr<gpu::DeviceLocalBuffer> ShaderPack::findRuntimeVKBuffer(RuntimeBuffer &runtimeBuffer,
                                                                       uint32_t frameIndex) {
    return std::as_const(*this).findRuntimeVKBuffer(std::as_const(runtimeBuffer), frameIndex);
}

std::shared_ptr<gpu::DeviceLocalBuffer> ShaderPack::findRuntimeVKBuffer(const RuntimeBuffer &runtimeBuffer,
                                                                       uint32_t frameIndex) const {
    return runtimeBuffer.frameBuffers[runtimeBuffer.config.shared ? 0 : frameIndex];
}

double ShaderPack::evaluateResourceExpression(const std::string &expression) const {
    shader_pack::ExpressionScope scope;
    for (const auto &variable : runtimeResourceExpressionVariables_) scope.add(variable.name, &variable.value);
    shader_pack::addAttributeNumbers(scope, attributeNumbers_, attributeNumbersError_);
    return scope.evaluate(expression);
}

VkExtent3D ShaderPack::runtimeTextureExtent(const ShaderPackLoader::TextureConfig &config) const {
    if (config.scale > 0.0f) {
        return {std::max<uint32_t>(1, static_cast<uint32_t>(referenceWidth_ * config.scale)),
                std::max<uint32_t>(1, static_cast<uint32_t>(referenceHeight_ * config.scale)), 1};
    }
    if (config.widthExpression.empty() || config.heightExpression.empty()) {
        throw std::runtime_error("runtime texture requires width/height or scale: " + config.name);
    }

    const double width = evaluateResourceExpression(config.widthExpression);
    const double height = evaluateResourceExpression(config.heightExpression);
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0) {
        throw std::runtime_error("invalid runtime texture extent: " + config.name);
    }
    VkExtent3D extent{wholeTexels(width), wholeTexels(height), 1};
    if (isLayered(config.dimension)) {
        if (config.depthExpression.empty()) {
            throw std::runtime_error("runtime layered texture requires depth: " + config.name);
        }
        const double depth = evaluateResourceExpression(config.depthExpression);
        if (!std::isfinite(depth) || depth <= 0.0) {
            throw std::runtime_error("invalid runtime texture depth: " + config.name);
        }
        extent.depth = wholeTexels(depth);
    }
    return extent;
}

size_t ShaderPack::runtimeBufferSize(const ShaderPackLoader::BufferConfig &config) const {
    const double size = std::ceil(evaluateResourceExpression(config.sizeExpression));
    // A size that is no byte count (negative, NaN, past 2^64) is an error in the pack.
    if (!(size >= 0.0 && size < 18446744073709551616.0)) {
        throw std::runtime_error("invalid runtime buffer size: " + config.name);
    }
    return std::max<size_t>(1, static_cast<size_t>(size));
}

void ShaderPack::initRuntimeTextures() {
    const auto framework = framework_.lock();
    const auto device = framework->device();
    const auto vma = framework->vma();
    const uint32_t frameCount = framework->swapchain()->imageCount();

    std::vector<RuntimeTexture> textures(shaderPack_.textures.size());
    shader_pack::parallelEach(textures.size(), [&](size_t index) {
        RuntimeTexture &texture = textures[index];
        texture.config = shaderPack_.textures[index];
        const auto &config = texture.config;
        if (config.sampledBinding.has_value()) {
            texture.sampler =
                gpu::Sampler::create(device, config.filter, VK_SAMPLER_MIPMAP_MODE_LINEAR, config.addressMode);
        }
        // An imported texture is made from its file (loadImportedTextures).
        if (config.imported) return;

        const VkExtent3D extent = runtimeTextureExtent(config);
        gpu::ImageDesc desc{.width = extent.width, .height = extent.height, .format = config.format};
        // Transfer destination so it can be cleared when made (prepareRuntimeResources).
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (config.dimension != Dimension::Texture3D) desc.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (config.storageBinding.has_value()) desc.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        switch (config.dimension) {
            case Dimension::Cube:
                desc.layers = 6;
                desc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
                // View 0 is the six faces as an array, 1-6 each face (to render into) and 7 the cube (to sample).
                texture.singleLayerViewBaseIndex = 1;
                texture.sampledViewIndex = 7;
                break;
            case Dimension::Texture2DArray:
                desc.layers = extent.depth;
                // View 0 is the whole array, 1 on each layer (to render into).
                texture.singleLayerViewBaseIndex = 1;
                break;
            case Dimension::Texture3D: desc.depth = extent.depth; break;
            case Dimension::Texture2D: break;
        }

        texture.frameImages.resize(config.shared ? 1 : frameCount);
        for (auto &image : texture.frameImages) {
            image = gpu::DeviceLocalImage::create(device, vma, desc);
            if (config.dimension == Dimension::Cube) {
                for (uint32_t face = 0; face < 6; face++) {
                    image->addImageView(layerView(*image, VK_IMAGE_VIEW_TYPE_2D, face, 1));
                }
                image->addImageView(layerView(*image, VK_IMAGE_VIEW_TYPE_CUBE, 0, 6));
            } else if (config.dimension == Dimension::Texture2DArray) {
                for (uint32_t layer = 0; layer < extent.depth; layer++) {
                    image->addImageView(layerView(*image, VK_IMAGE_VIEW_TYPE_2D, layer, 1));
                }
            }
        }
    });

    runtimeTextures_ = std::move(textures);
    runtimeTextureIndices_.clear();
    for (size_t i = 0; i < runtimeTextures_.size(); i++) {
        runtimeTextureIndices_.insert_or_assign(runtimeTextures_[i].config.name, i);
    }
}

void ShaderPack::initRuntimeBuffers() {
    const auto framework = framework_.lock();
    const auto device = framework->device();
    const auto vma = framework->vma();
    const uint32_t frameCount = framework->swapchain()->imageCount();

    std::vector<RuntimeBuffer> buffers(shaderPack_.buffers.size());
    shader_pack::parallelEach(buffers.size(), [&](size_t index) {
        RuntimeBuffer &buffer = buffers[index];
        buffer.config = shaderPack_.buffers[index];
        const size_t size = runtimeBufferSize(buffer.config);
        buffer.frameBuffers.resize(buffer.config.shared ? 1 : frameCount);
        for (auto &frame : buffer.frameBuffers) frame = makeRuntimeBuffer(device, vma, size);
    });

    runtimeBuffers_ = std::move(buffers);
    runtimeBufferIndices_.clear();
    for (size_t i = 0; i < runtimeBuffers_.size(); i++) {
        runtimeBufferIndices_.insert_or_assign(runtimeBuffers_[i].config.name, i);
    }
}

void ShaderPack::loadImportedTextures() {
    const auto framework = framework_.lock();
    const auto device = framework->device();
    const auto vma = framework->vma();

    shader_pack::parallelEach(runtimeTextures_.size(), [&](size_t index) {
        RuntimeTexture &texture = runtimeTextures_[index];
        const auto &config = texture.config;
        if (!config.imported) return;

        // The host copy goes once the upload has run.
        gpu::ImageDesc desc{.format = config.format, .usage = VK_IMAGE_USAGE_SAMPLED_BIT};
        if (config.dimension == Dimension::Texture2D) {
            if (config.format != VK_FORMAT_R8G8B8A8_UNORM) {
                throw std::runtime_error("Imported 2d textures currently require R8G8B8A8_UNORM: " + config.name);
            }
            const auto pixels =
                gpu::ImageLoader::create(std::vector<std::string>{config.sourcePath.string()}, 4, false);
            desc.width = pixels->width();
            desc.height = pixels->height();
            texture.importedImage = gpu::DeviceLocalImage::create(device, vma, desc);
            texture.importedImage->uploadToStagingBuffer(pixels->data());
            return;
        }

        // Layered textures are raw texels, layer after layer.
        if (!isLayered(config.dimension)) {
            throw std::runtime_error("unsupported imported texture dimension: " + config.name);
        }
        const bool array = config.dimension == Dimension::Texture2DArray;
        const size_t expectedSize = static_cast<size_t>(config.importedWidth) * config.importedHeight *
                                    config.importedDepth * gpu::formatToByte(config.format);
        std::vector<uint8_t> texels = readTextureFile(config.sourcePath);
        if (texels.size() != expectedSize) {
            throw std::runtime_error(std::string("imported ") + (array ? "2d_array" : "3d") +
                                     " texture size mismatch for " + config.name + ": expected " +
                                     std::to_string(expectedSize) + " bytes but got " + std::to_string(texels.size()));
        }
        desc.width = config.importedWidth;
        desc.height = config.importedHeight;
        if (array) {
            desc.layers = config.importedDepth;
        } else {
            desc.depth = config.importedDepth;
        }
        texture.importedImage = gpu::DeviceLocalImage::create(device, vma, desc);
        texture.importedImage->uploadToStagingBuffer(texels.data());
    });
}

void ShaderPack::prepareRuntimeResources() {
    // New images and buffers hold whatever memory they were given. The pack's passes read some of them before they
    // first write them (history, accumulation, caches), and that showed as large pink, green and blue blotches over
    // the world for seconds after every settings change or window resize, until the history washed them out.
    std::vector<gpu::DeviceLocalImage *> cleared;
    std::vector<gpu::DeviceLocalImage *> imported;
    std::vector<VkBuffer> buffers;
    for (auto &texture : runtimeTextures_) {
        if (texture.importedImage != nullptr) imported.push_back(texture.importedImage.get());
        for (auto &image : texture.frameImages) {
            if (image != nullptr) cleared.push_back(image.get());
        }
    }
    for (auto &buffer : runtimeBuffers_) {
        for (auto &frame : buffer.frameBuffers) {
            if (frame != nullptr) buffers.push_back(frame->vkBuffer());
        }
    }
    if (cleared.empty() && imported.empty() && buffers.empty()) return;

    submitAndWait(*framework_.lock(), [&](gpu::CommandBuffer &commandBuffer) {
        // Cleared textures are left in GENERAL, the layout the passes bind them in; imported ones are sampled.
        std::vector<gpu::ImageUse> uses;
        uses.reserve(cleared.size() + imported.size());
        for (auto *image : cleared) {
            uses.emplace_back(image, gpu::ImageUsage::General, VK_PIPELINE_STAGE_2_TRANSFER_BIT, true);
        }
        for (auto *image : imported) uses.emplace_back(image, gpu::ImageUsage::TransferDst, 0, true);
        commandBuffer.sync(uses);

        const VkCommandBuffer commands = commandBuffer.vkCommandBuffer();
        const VkClearColorValue zero{};
        for (auto *image : cleared) {
            vkCmdClearColorImage(commands, image->vkImage(), VK_IMAGE_LAYOUT_GENERAL, &zero, 1,
                                 &gpu::wholeColorSubresourceRange);
        }
        for (VkBuffer buffer : buffers) vkCmdFillBuffer(commands, buffer, 0, VK_WHOLE_SIZE, 0);
        for (auto *image : imported) image->uploadToImage(commands);

        uses.clear();
        for (auto *image : imported) uses.emplace_back(image, gpu::ImageUsage::Sampled);
        commandBuffer.sync(uses);
    });
}
