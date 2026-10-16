#pragma once

#include "prism/render/modules/world/nrd/nrd_wrapper.hpp"
#include "prism/render/modules/world/world_module.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct NrdModuleContext;

// Denoises the path tracer's first-hit lighting with NVIDIA's NRD. A prepare pass turns the tracer's images into NRD's
// inputs, NRD denoises the diffuse and specular radiance, and a compose pass builds the frame from the result.
class NrdModule : public WorldModule, public SharedObject<NrdModule> {
    friend struct NrdModuleContext;

  public:
    static constexpr auto NAME = "render_pipeline.module.nrd.name";
    static constexpr uint32_t inputImageNum = 15;
    static constexpr uint32_t outputImageNum = 1;

    NrdModule();
    ~NrdModule();

    void init(std::shared_ptr<Framework> framework, std::shared_ptr<WorldPipeline> worldPipeline);

    bool setOrCreateInputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                std::vector<VkFormat> &formats,
                                uint32_t frameIndex) override;

    bool setOrCreateOutputImages(std::vector<std::shared_ptr<gpu::DeviceLocalImage>> &images,
                                 std::vector<VkFormat> &formats,
                                 uint32_t frameIndex) override;

    void build() override;

    void setAttributes(int attributeCount, std::vector<std::string> &attributeKVs) override;
    // Every attribute: they only feed the denoiser's settings, which NRD takes between any two frames.
    bool setRuntimeAttribute(const std::string &key, const std::string &value) override;

    std::vector<std::shared_ptr<WorldModuleContext>> &contexts() override;

    void bindTexture(std::shared_ptr<gpu::Sampler> sampler,
                     std::shared_ptr<gpu::DeviceLocalImage> image,
                     int index) override;

    void preClose() override;

  private:
    struct Frame;

    void buildFrame(Frame &frame, const std::shared_ptr<gpu::Device> &device);
    // Records the frame's prepare, denoise and compose passes into the context's world command buffer.
    void record(FrameworkContext &context);

    // The module's attributes configure REBLUR; they reach NRD only when REBLUR is the denoiser that runs.
    nrd::ReblurSettings reblurSettings_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    // NRD's frame counter. Every build restarts it, and with it NRD's history.
    uint32_t nrdFrameIndex_ = 0;

    std::shared_ptr<NrdWrapper> wrapper_;
    // What NRD reads (written by the prepare pass) and writes (read by the compose pass). Frames run one after another
    // on the queue and NRD keeps its own history, so every frame slot uses the same images.
    NrdUserTextures nrdTextures_;
    std::shared_ptr<gpu::ComputePipeline> preparePipeline_;
    std::shared_ptr<gpu::ComputePipeline> composePipeline_;
    std::vector<std::unique_ptr<Frame>> frames_;
    std::vector<std::shared_ptr<WorldModuleContext>> contexts_;
};
