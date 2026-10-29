#include "prism/render/buffers.hpp"

#include "prism/render/chunks.hpp"
#include "prism/render/entities.hpp"
#include "prism/render/render_framework.hpp"
#include "prism/render/renderer.hpp"
#include "prism/render/world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

// The C++ layout must be the std140 one the shaders see: a size check alone missed offHandLightPos sitting 4 bytes
// early. BufferProxy.WORLD_UBO_SIZE on the Java side fills everything before packParams; the two must move together.
static_assert(offsetof(gpu::Data::WorldUBO, windTime) == 692); // BufferProxy.WIND_OFFSET
static_assert(offsetof(gpu::Data::WorldUBO, offHandLightPos) == 704);
static_assert(offsetof(gpu::Data::WorldUBO, packParams) == 736);
static_assert(sizeof(gpu::Data::WorldUBO) == 736 + 16 * PRISM_PACK_PARAM_COUNT);

namespace {

// The light meter holds 256 samples of a vec4 (the cube root of the incident light per channel, and the sample's
// weight); one more with how much of a day it is (1 by day, 0 at night); one with the experimental Bedrock profile's
// meter state (exposure.comp); then that profile's history of each sample: 256 of the light accumulated and 256 of
// the surface it lies on (world.rgen).
constexpr size_t LIGHT_METER_BYTES = (256 + 1 + 1 + 256 + 256) * 4 * sizeof(float);

// HostVisibleBuffer::uploadToBuffer only reads from its source.
void write(gpu::HostVisibleBuffer &buffer, const void *src, size_t size, size_t offset = 0) {
    buffer.uploadToBuffer(const_cast<void *>(src), size, offset);
}

std::shared_ptr<gpu::HostVisibleBuffer> &ensureUniform(std::shared_ptr<gpu::HostVisibleBuffer> &buffer,
                                                       Framework &framework,
                                                       size_t size,
                                                       VkBufferUsageFlags usage) {
    if (buffer == nullptr) {
        buffer = gpu::HostVisibleBuffer::create(framework.vma(), framework.device(), size,
                                                usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }
    return buffer;
}

uint32_t low32(VkDeviceAddress address) {
    return static_cast<uint32_t>(address & 0xffffffffull);
}

uint32_t high32(VkDeviceAddress address) {
    return static_cast<uint32_t>(address >> 32u);
}

// Point `index` of the Halton (2, 3) sequence, after https://www.shadertoy.com/view/wdXSW8. The loop stops when the
// base 3 digits run out, which cuts the base 2 coordinate short of its later digits; the camera jitter has always
// been this exact sequence, and the temporal passes see it.
glm::vec2 halton(int index) {
    float remainingX = static_cast<float>(index);
    float remainingY = static_cast<float>(index);
    float weightX = 1.0f;
    float weightY = 1.0f;
    glm::vec2 point(0.0f);
    while (remainingX > 0.0f && remainingY > 0.0f) {
        weightX = weightX / 2.0f;
        weightY = weightY / 3.0f;
        point.x += weightX * std::fmod(remainingX, 2.0f);
        point.y += weightY * std::fmod(remainingY, 3.0f);
        remainingX = std::floor(remainingX / 2.0f);
        remainingY = std::floor(remainingY / 3.0f);
    }
    return point;
}

VkBufferMemoryBarrier2 bufferBarrier(VkBuffer buffer,
                                     uint32_t queueFamily,
                                     VkPipelineStageFlags2 srcStages,
                                     VkAccessFlags2 srcAccess,
                                     VkPipelineStageFlags2 dstStages,
                                     VkAccessFlags2 dstAccess) {
    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStages;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStages;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = queueFamily;
    barrier.dstQueueFamilyIndex = queueFamily;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

void recordBarriers(VkCommandBuffer commandBuffer, const std::vector<VkBufferMemoryBarrier2> &barriers) {
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependency.pBufferMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

} // namespace

Buffers::Buffers(std::shared_ptr<Framework> framework) : framework_(framework) {
    const uint32_t frames = framework->swapchain()->imageCount();
    uniforms_.resize(frames);

    // Before the first frame there is no previous one: its camera is the identity.
    lastWorld_.cameraViewMat = glm::mat4(1.0f);
    lastWorld_.cameraEffectedViewMat = glm::mat4(1.0f);
    lastWorld_.cameraProjMat = glm::mat4(1.0f);
    lastWorld_.cameraViewMatInv = glm::mat4(1.0f);
    lastWorld_.cameraEffectedViewMatInv = glm::mat4(1.0f);
    lastWorld_.cameraProjMatInv = glm::mat4(1.0f);

    seedEngine_.seed(std::random_device{}());
}

std::shared_ptr<Framework> Buffers::framework() const {
    auto framework = framework_.lock();
    if (framework == nullptr) throw std::logic_error("Buffers used after the renderer closed");
    return framework;
}

uint32_t Buffers::currentFrame() const {
    return framework()->safeAcquireCurrentContext()->frameIndex;
}

void Buffers::performQueuedUpload() {
    auto framework = this->framework();
    auto context = framework->safeAcquireCurrentContext();
    const uint32_t queueFamily = framework->physicalDevice()->mainQueueIndex();
    const std::shared_ptr<gpu::CommandBuffer> &commandBuffer = context->uploadCommandBuffer;

    std::scoped_lock lock(mutex_);
    if (importantUploads_.empty()) return;

    // Earlier transfers out of these buffers finish before they are overwritten.
    uploadBarriers_.clear();
    for (const auto &buffer : importantUploads_) {
        uploadBarriers_.push_back(bufferBarrier(buffer->vkBuffer(), queueFamily, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                VK_ACCESS_2_MEMORY_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                VK_ACCESS_2_MEMORY_WRITE_BIT));
    }
    recordBarriers(commandBuffer->vkCommandBuffer(), uploadBarriers_);

    for (const auto &buffer : importantUploads_) buffer->uploadToBuffer(commandBuffer);

    // World geometry feeds acceleration structure builds and ray tracing, and the stages that read it as plain data.
    // Section buffers also hold the inputs of their opacity micromaps, where the device has them.
    const VkPipelineStageFlags2 worldReaders =
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
        VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
        VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR |
        (gpu::opacityMicromapsEnabled() ? VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT : VK_PIPELINE_STAGE_2_NONE);
    constexpr VkAccessFlags2 readWrite = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    uploadBarriers_.clear();
    for (const auto &buffer : importantUploads_) {
        uploadBarriers_.push_back(bufferBarrier(buffer->vkBuffer(), queueFamily, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                readWrite, worldReaders, readWrite));
    }
    recordBarriers(commandBuffer->vkCommandBuffer(), uploadBarriers_);
    importantUploads_.clear();
}

void Buffers::queueImportantWorldUpload(std::shared_ptr<gpu::DeviceLocalBuffer> buffer) {
    if (buffer == nullptr) return;
    std::scoped_lock lock(mutex_);
    importantUploads_.push_back(std::move(buffer));
}

void Buffers::setAndUploadWorldUniformBuffer(gpu::Data::WorldUBO &ubo) {
    auto framework = this->framework();
    const uint32_t frame = framework->safeAcquireCurrentContext()->frameIndex;

    // Minecraft's projection is OpenGL's: y points up and depth runs from -1 to 1. Vulkan's y points down and its
    // depth runs from 0 to 1.
    glm::mat4 glToVulkan(1.0f);
    glToVulkan[1][1] = -1.0f;
    glToVulkan[2][2] = 0.5f;
    glToVulkan[3][2] = 0.5f;
    ubo.cameraProjMat = glToVulkan * ubo.cameraProjMat;
    ubo.cameraViewMatInv = glm::inverse(ubo.cameraViewMat);
    ubo.cameraEffectedViewMatInv = glm::inverse(ubo.cameraEffectedViewMat);
    ubo.cameraProjMatInv = glm::inverse(ubo.cameraProjMat);

    // What the world, the chunks and the entities publish is read before taking the lock: they take theirs, then
    // call in here.
    ubo.entityLightCount = 0;
    ubo.entityLightAddressLo = 0;
    ubo.entityLightAddressHi = 0;
    ubo.entityLightPad = 0;
    ubo.lodCoverageAddressLo = 0;
    ubo.lodCoverageAddressHi = 0;
    ubo.lodCoveragePad0 = 0;
    ubo.lodCoveragePad1 = 0;
    ubo.cameraPos = glm::dvec4(0.0);
    ubo.chunkGridInfo = glm::ivec4(0);
    ubo.chunkStorageSectionPos = glm::ivec4(0);
    if (auto world = Renderer::instance().world(); world != nullptr) {
        ubo.cameraPos = glm::dvec4(world->getCameraPos(), 0.0);
        if (auto chunks = world->chunks(); chunks != nullptr) {
            const VkDeviceAddress coverage = chunks->lodCoverageAddress();
            ubo.lodCoverageAddressLo = low32(coverage);
            ubo.lodCoverageAddressHi = high32(coverage);
            ubo.chunkGridInfo = chunks->chunkGridInfo();
            ubo.chunkStorageSectionPos = chunks->chunkStorageSectionPos();
        }
        if (auto entities = world->entities(); entities != nullptr) {
            const VkDeviceAddress lights = entities->lightBufferAddress();
            const uint32_t lightCount = entities->lightCount();
            if (lights != 0 && lightCount > 0) {
                ubo.entityLightCount = lightCount;
                ubo.entityLightAddressLo = low32(lights);
                ubo.entityLightAddressHi = high32(lights);
            }
        }
    }

    std::scoped_lock lock(mutex_);
    ubo.seed = static_cast<uint32_t>(seedDistribution_(seedEngine_));
    ubo.frameCounter = frameCounter_++;
    std::copy(packParams_.begin(), packParams_.end(), ubo.packParams);
    ubo.cameraJitter = useJitter_ ? halton(static_cast<int>(jitterIndex_++)) - glm::vec2(0.5f) : glm::vec2(0.0f);

    FrameUniforms &uniforms = uniforms_[frame];
    constexpr VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    write(*ensureUniform(uniforms.world, *framework, sizeof(ubo), usage), &ubo, sizeof(ubo));
    write(*ensureUniform(uniforms.lastWorld, *framework, sizeof(ubo), usage), &lastWorld_, sizeof(lastWorld_));
    lastWorld_ = ubo;
}

gpu::Data::WorldUBO Buffers::lastWrittenWorldUniform() {
    std::scoped_lock lock(mutex_);
    return lastWorld_;
}

void Buffers::setPackParams(const std::array<glm::vec4, PRISM_PACK_PARAM_COUNT> &params) {
    std::scoped_lock lock(mutex_);
    packParams_ = params;
    // A rebuild runs inside a frame whose world uniform was already written, with the previous pack's values (all
    // zero at startup: no bounces, no sun). Every slot's uniform gets the new ones; the queue is idle during a rebuild.
    for (auto &uniforms : uniforms_) {
        if (uniforms.world != nullptr) {
            write(*uniforms.world, packParams_.data(), sizeof(packParams_), offsetof(gpu::Data::WorldUBO, packParams));
        }
    }
}

void Buffers::setPackParam(uint32_t slot, const glm::vec4 &value) {
    std::scoped_lock lock(mutex_);
    if (slot < packParams_.size()) packParams_[slot] = value;
}

void Buffers::setUseJitter(bool useJitter) {
    std::scoped_lock lock(mutex_);
    useJitter_ = useJitter;
}

void Buffers::setAndUploadSkyUniformBuffer(const gpu::Data::SkyUBO &ubo) {
    auto framework = this->framework();
    const uint32_t frame = framework->safeAcquireCurrentContext()->frameIndex;
    std::scoped_lock lock(mutex_);
    auto &buffer = ensureUniform(uniforms_[frame].sky, *framework, sizeof(ubo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    write(*buffer, &ubo, sizeof(ubo));
    cameraSubmersionType_ = ubo.cameraSubmersionType;
}

uint32_t Buffers::cameraSubmersionType() {
    std::scoped_lock lock(mutex_);
    return cameraSubmersionType_;
}

void Buffers::setAndUploadTextureMappingBuffer(const gpu::Data::TextureMapping &mapping) {
    auto framework = this->framework();
    const uint32_t frame = framework->safeAcquireCurrentContext()->frameIndex;
    std::scoped_lock lock(mutex_);
    auto &buffer =
        ensureUniform(uniforms_[frame].textureMapping, *framework, sizeof(mapping), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    write(*buffer, &mapping, sizeof(mapping));
}

std::shared_ptr<gpu::HostVisibleBuffer>
Buffers::uniform(std::shared_ptr<gpu::HostVisibleBuffer> FrameUniforms::*member) {
    const uint32_t frame = currentFrame();
    std::scoped_lock lock(mutex_);
    return uniforms_[frame].*member;
}

std::shared_ptr<gpu::HostVisibleBuffer> Buffers::worldUniformBuffer() {
    return uniform(&FrameUniforms::world);
}

std::shared_ptr<gpu::HostVisibleBuffer> Buffers::lastWorldUniformBuffer() {
    return uniform(&FrameUniforms::lastWorld);
}

std::shared_ptr<gpu::HostVisibleBuffer> Buffers::skyUniformBuffer() {
    return uniform(&FrameUniforms::sky);
}

std::shared_ptr<gpu::HostVisibleBuffer> Buffers::textureMappingBuffer() {
    return uniform(&FrameUniforms::textureMapping);
}

std::shared_ptr<gpu::DeviceLocalBuffer> Buffers::lightMeterBuffer() {
    std::scoped_lock lock(mutex_);
    if (lightMeter_ == nullptr) {
        auto framework = this->framework();
        lightMeter_ = gpu::DeviceLocalBuffer::create(framework->vma(), framework->device(), LIGHT_METER_BYTES,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }
    return lightMeter_;
}
