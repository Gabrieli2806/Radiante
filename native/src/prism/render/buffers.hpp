#pragma once

#include "common/shared.hpp"
#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <vector>

class Framework;

// The uniforms every frame reads and the buffer uploads every frame starts with.
//
// The uniforms (world, previous world, sky, texture mapping) are written by the CPU into host-visible buffers, one set
// per frame slot, so a frame in flight keeps the values it was recorded with. Device-local buffers whose new contents
// sit in their staging memory (chunk and entity geometry, lights) are queued, and performQueuedUpload records their
// copies into the frame's upload command buffer.
//
// The lock guards this object's own state only. It is never held while asking the framework for the current frame or
// while reading the world, the chunks or the entities: those call in here while holding their own locks.
class Buffers : public SharedObject<Buffers> {
  public:
    explicit Buffers(std::shared_ptr<Framework> framework);

    // Records every queued copy into the current frame's upload command buffer, with the barriers around them.
    void performQueuedUpload();

    // Queues the copy of a buffer's staging memory into the buffer. It stays queued until a frame records it: a frame
    // dropped before it got that far (a rebuild) must not lose it.
    void queueImportantWorldUpload(std::shared_ptr<gpu::DeviceLocalBuffer> buffer);

    // Completes the world uniform Java filled (everything before packParams) and writes it to the current frame's
    // buffer; the one written the time before goes to lastWorldUniformBuffer. ubo is left holding what was written.
    void setAndUploadWorldUniformBuffer(gpu::Data::WorldUBO &ubo);
    // The shader pack's live settings (WorldUBO.packParams), copied into every frame's world uniform. They live here
    // because the shader pack is recreated by every rebuild and Buffers is not.
    void setPackParams(const std::array<glm::vec4, PRISM_PACK_PARAM_COUNT> &params);
    void setPackParam(uint32_t slot, const glm::vec4 &value);
    // Whether the camera moves by a sub-pixel jitter from frame to frame, for the temporal passes and upscalers.
    void setUseJitter(bool useJitter);
    void setAndUploadSkyUniformBuffer(const gpu::Data::SkyUBO &ubo);
    void setAndUploadTextureMappingBuffer(const gpu::Data::TextureMapping &mapping);

    // The current frame's copies; null until the first upload of each.
    std::shared_ptr<gpu::HostVisibleBuffer> worldUniformBuffer();
    std::shared_ptr<gpu::HostVisibleBuffer> lastWorldUniformBuffer();
    std::shared_ptr<gpu::HostVisibleBuffer> skyUniformBuffer();
    std::shared_ptr<gpu::HostVisibleBuffer> textureMappingBuffer();
    // The incident light meter: the ray generation shader writes one sample per cell of a 16 x 16 grid each frame,
    // tone mapping's exposure pass reads them (metering mode "incident"). Shared by both modules, made on first use.
    std::shared_ptr<gpu::DeviceLocalBuffer> lightMeterBuffer();

  private:
    struct FrameUniforms {
        std::shared_ptr<gpu::HostVisibleBuffer> world;
        std::shared_ptr<gpu::HostVisibleBuffer> lastWorld;
        std::shared_ptr<gpu::HostVisibleBuffer> sky;
        std::shared_ptr<gpu::HostVisibleBuffer> textureMapping;
    };

    std::shared_ptr<Framework> framework() const;
    uint32_t currentFrame() const;
    std::shared_ptr<gpu::HostVisibleBuffer> uniform(std::shared_ptr<gpu::HostVisibleBuffer> FrameUniforms::*member);

    std::weak_ptr<Framework> framework_;
    std::mutex mutex_;

    std::vector<FrameUniforms> uniforms_;
    std::array<glm::vec4, PRISM_PACK_PARAM_COUNT> packParams_{};
    gpu::Data::WorldUBO lastWorld_{};
    bool useJitter_ = true;
    size_t jitterIndex_ = 0;
    uint32_t frameCounter_ = 0;
    std::ranlux48 seedEngine_;
    std::uniform_int_distribution<int32_t> seedDistribution_;
    std::shared_ptr<gpu::DeviceLocalBuffer> lightMeter_;

    std::vector<std::shared_ptr<gpu::DeviceLocalBuffer>> importantUploads_;
    // Reused by every performQueuedUpload.
    std::vector<VkBufferMemoryBarrier2> uploadBarriers_;
};
