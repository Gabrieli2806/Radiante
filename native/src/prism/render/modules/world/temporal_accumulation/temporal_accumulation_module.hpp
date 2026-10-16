#pragma once

#include "prism/render/modules/world/world_module.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Framework;
class FrameworkContext;
class WorldPipeline;
struct WorldModuleContext;

// The push constants of tmp_acc.frag.
struct TemporalAccumulationPushConstant {
    float alpha;     // this frame's weight where the history is kept
    float threshold; // the least cosine between this frame's normal and the history's that keeps the history
};
static_assert(sizeof(TemporalAccumulationPushConstant) == 8, "the layout tmp_acc.frag reads");

struct TemporalAccumulationModuleContext;

// Temporal accumulation (tmp_acc.frag): each frame's radiance blended into a history that follows the motion vectors,
// with the history dropped where it comes from off screen or from a surface that faced another way. The history,
// radiance and normals, is one pair of images all frames in flight share: a frame draws into targets of its own, which
// are then copied over the history for the next frame.
class TemporalAccumulationModule : public WorldModule, public SharedObject<TemporalAccumulationModule> {
    friend TemporalAccumulationModuleContext;

  public:
    constexpr static std::string_view NAME = "render_pipeline.module.temporal_accumulation.name";
    constexpr static uint32_t inputImageNum = 3;  // radiance, motion vectors, normal and roughness
    constexpr static uint32_t outputImageNum = 1; // the accumulated radiance

    TemporalAccumulationModule();

    void init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline);

    bool setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                std::vector<VkFormat> &formats,
                                uint32_t frameIndex) override;
    bool setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                 std::vector<VkFormat> &formats,
                                 uint32_t frameIndex) override;

    void setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) override;

    void build() override;
    std::vector<std::shared_ptr<WorldModuleContext>> &contexts() override;

    void bindTexture(std::shared_ptr<gpu::Sampler> sampler,
                     std::shared_ptr<gpu::DeviceLocalImage> image,
                     int index) override;

    void preClose() override;

  private:
    // Slots in the module's input list, in the order modules/temporal_accumulation.yaml declares them.
    enum Input : uint32_t { Radiance, MotionVector, NormalRoughness };

    // What one frame in flight draws with.
    struct Frame {
        std::array<std::shared_ptr<gpu::DeviceLocalImage>, inputImageNum> inputs;
        std::shared_ptr<gpu::DeviceLocalImage> radiance; // the output, the accumulated radiance
        std::shared_ptr<gpu::DeviceLocalImage> normal;   // the accumulated normals, drawn beside it
        std::shared_ptr<gpu::DescriptorTable> descriptors;
        std::shared_ptr<gpu::Framebuffer> framebuffer;
    };

    void createImages(Framework &framework);
    void createDescriptorTables(Framework &framework);
    void createPipeline(Framework &framework);
    void record(uint32_t frameIndex, gpu::CommandBuffer &commandBuffer);

    std::vector<Frame> frames_;
    // The last frame's result, which this frame reads through the motion vectors.
    std::shared_ptr<gpu::DeviceLocalImage> historyRadiance_;
    std::shared_ptr<gpu::DeviceLocalImage> historyNormal_;
    std::shared_ptr<gpu::Sampler> sampler_;
    std::shared_ptr<gpu::RenderPass> renderPass_;
    std::shared_ptr<gpu::GraphicsPipeline> pipeline_;
    std::vector<std::shared_ptr<WorldModuleContext>> contexts_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

struct TemporalAccumulationModuleContext : public WorldModuleContext, SharedObject<TemporalAccumulationModuleContext> {
    TemporalAccumulationModuleContext(std::shared_ptr<FrameworkContext> frameworkContext,
                                      std::shared_ptr<WorldPipelineContext> worldPipelineContext,
                                      std::shared_ptr<TemporalAccumulationModule> temporalAccumulationModule);

    void render() override;

  private:
    std::weak_ptr<TemporalAccumulationModule> module_;
    uint32_t frameIndex_;
};
