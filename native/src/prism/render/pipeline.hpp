#pragma once

#include "common/shared.hpp"
#include "common/singleton.hpp"
#include "prism/all_extern.hpp"
#include "prism/gpu/gpu.hpp"
#include "prism/render/shader_prewarm.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The world pipeline as Java's Pipeline.buildNative lays it out in memory: keep the layout. Modules come in execution
// order; images are referred to by index, and image 0 is the picture the pipeline ends with.
struct WorldPipelineBuildParams {
    int moduleCount;
    int padding;
    char **moduleNames;
    int *imageFormats;    // VkFormat of each image, by index
    int **inputIndices;   // per module, the indices of the images it reads
    int **outputIndices;  // per module, the indices of the images it writes
    int *attributeCounts; // per module, how many attributes it has
    char ***attributeKVs; // per module, its attributes' names and values one after the other
};
static_assert(offsetof(WorldPipelineBuildParams, moduleNames) == 8 &&
                  offsetof(WorldPipelineBuildParams, attributeKVs) == 48 && sizeof(WorldPipelineBuildParams) == 56,
              "must match the block Pipeline.buildNative writes");

class Framework;
class FrameworkContext;
class WorldModule;
struct WorldModuleContext;
class ShaderPack;

class WorldPipeline;
struct WorldPipelineContext;

class Pipeline;
struct PipelineContext;

// What a world pipeline is made of, as Java described it: the modules, the images connecting them and the modules'
// attributes. Kept between builds, so a rebuild that only changes the size (a window resize, vsync) needs nothing else.
class WorldPipelineBlueprint : public SharedObject<WorldPipelineBlueprint> {
    friend WorldPipeline;

  public:
    // Throws when the image indices are not 0, 1, 2... without gaps.
    WorldPipelineBlueprint(WorldPipelineBuildParams *params);

    // Records a value changed without a rebuild (an attribute of moduleName, by its full name), so a rebuild from
    // this blueprint alone (a window resize, vsync) keeps it rather than going back to the value it was built with.
    void setAttributeValue(std::string_view moduleName, const std::string &key, const std::string &value);
    // The attributes of the first module named moduleName (name, value, name, value...); null without one.
    const std::vector<std::string> *attributesOf(std::string_view moduleName) const;

  private:
    struct ModuleDesc {
        std::string name;
        std::vector<uint32_t> inputs;
        std::vector<uint32_t> outputs;
        std::vector<std::string> attributeKVs; // name, value, name, value...
    };

    std::vector<ModuleDesc> modules_;
    std::vector<VkFormat> imageFormats_;
};

// The world pipeline built from a blueprint: its modules, and per frame slot the images they share.
class WorldPipeline : public SharedObject<WorldPipeline> {
  public:
    // The layout the final picture rests in between frames: render() leaves it there after the last pass, and the
    // framework's blit into Minecraft's target puts it back there.
#ifdef USE_AMD
    static constexpr VkImageLayout OUTPUT_LAYOUT = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
#else
    static constexpr VkImageLayout OUTPUT_LAYOUT = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
#endif

    WorldPipeline();

    // Builds every module of pipeline's blueprint; throws when a module cannot be made or built.
    void init(std::shared_ptr<Framework> framework, std::shared_ptr<Pipeline> pipeline);

    std::vector<std::shared_ptr<WorldModule>> &worldModules();
    // Image the pipeline blueprint assigned to `index` for the frame slot, or null when there is none.
    std::shared_ptr<gpu::DeviceLocalImage> sharedImage(uint32_t frameIndex, uint32_t index);
    std::vector<std::shared_ptr<WorldPipelineContext>> &contexts();
    // The shader pack the ray tracing module was built with; null without that module.
    std::shared_ptr<ShaderPack> shaderPack();

    // Hands an attribute to the built modules named moduleName (WorldModule::setRuntimeAttribute). False when the
    // pipeline has no such module or one of them takes the attribute only at a build.
    bool setRuntimeAttribute(std::string_view moduleName, const std::string &key, const std::string &value);

    void bindTexture(std::shared_ptr<gpu::Sampler> sampler, std::shared_ptr<gpu::DeviceLocalImage> image, int index);

  private:
    // Loads the shader pack the ray tracing module is built with; false when the pipeline has no such module.
    bool loadShaderPack(const std::shared_ptr<Framework> &framework, const WorldPipelineBlueprint &blueprint);
    // Makes the blueprint's module `index`, hands it its images for every frame slot and builds it.
    void buildModule(const std::shared_ptr<Framework> &framework,
                     WorldPipelineBlueprint &blueprint,
                     size_t index,
                     uint32_t frameCount);

    std::vector<std::shared_ptr<WorldModule>> worldModules_;
    // The blueprint's name of each module, by the same index: what a live attribute change looks them up by.
    std::vector<std::string> worldModuleNames_;
    std::vector<std::vector<std::shared_ptr<gpu::DeviceLocalImage>>> sharedImages_;
    std::shared_ptr<ShaderPack> shaderPack_;

    std::vector<std::shared_ptr<WorldPipelineContext>> contexts_;
};

// The world pipeline's work for one frame slot.
struct WorldPipelineContext : public SharedObject<WorldPipelineContext> {
    std::weak_ptr<FrameworkContext> frameworkContext;
    std::weak_ptr<WorldPipeline> worldPipeline;

    // The final picture of this frame slot, and every module's context for it in execution order. Filled in once the
    // modules are built.
    std::shared_ptr<gpu::DeviceLocalImage> outputImage;
    std::vector<std::shared_ptr<WorldModuleContext>> worldModuleContexts;

    WorldPipelineContext(std::shared_ptr<FrameworkContext> frameworkContext,
                         std::shared_ptr<WorldPipeline> worldPipeline);

    // Records every module's work into the frame's world command buffer and leaves the picture in OUTPUT_LAYOUT.
    void render();
    // Records a blit of the picture, scaled, into target (an image Minecraft keeps in GENERAL layout) and puts the
    // picture back in OUTPUT_LAYOUT.
    void blitOutput(gpu::CommandBuffer &commandBuffer, gpu::Image &target, uint32_t queueFamily);
};

// Owns the world pipeline across rebuilds, and the registry of the modules a pipeline can be made of.
class Pipeline : public SharedObject<Pipeline> {
  public:
    using WorldModuleConstructor =
        std::function<std::shared_ptr<WorldModule>(std::shared_ptr<Framework>, std::shared_ptr<WorldPipeline>)>;

    // The modules this device can run, by name, with how many images each reads and writes, and what has to be
    // released once for all when the renderer closes. Filled by collectWorldModules.
    static std::map<std::string, WorldModuleConstructor> worldModuleConstructors;
    static std::map<std::string, std::pair<uint32_t, uint32_t>> worldModuleInOutImageNums;
    static std::map<std::string, std::function<void()>> worldModuleStaticPreCloser;
    static void collectWorldModules();

    // Brackets a rebuild Java asked for, so its progress can be shown while it runs.
    static void beginNativeRebuild();
    static void endNativeRebuild();
    static bool nativeRebuildActive();
    // Shaders compiled so far in the rebuild under way, and how many it needs: for the progress shown to the player.
    static std::atomic<uint32_t> rebuildShadersDone;
    static std::atomic<uint32_t> rebuildShadersTotal;
    // The same for the passes whose pipelines are created after the shaders (the driver compiles them, slowly).
    static std::atomic<uint32_t> rebuildPassesDone;
    static std::atomic<uint32_t> rebuildPassesTotal;

    Pipeline();
    ~Pipeline();

    void init(std::shared_ptr<Framework> framework);
    // Takes Java's description of the pipeline; the next frame (or Framework::recreatePending) builds it.
    void buildWorldPipelineBlueprint(WorldPipelineBuildParams *params);
    // Starts compiling in the background what a pipeline built from Java's description will need (see
    // ShaderPrewarm), restarting a prewarm already running; the current pipeline and blueprint stay as they are.
    // False when there is nothing it can warm: no ray tracing module was built, the last rebuild failed, or the
    // description names no ray tracing module or another pack. Call it under the framework's frame lock.
    bool prewarm(WorldPipelineBuildParams *params);
    ShaderPrewarm &shaderPrewarm();
    // Replaces the world pipeline with one built from the blueprint, for the framework's current frame contexts.
    void recreate(std::shared_ptr<Framework> framework);
    void close();
    std::shared_ptr<PipelineContext> acquirePipelineContext(std::shared_ptr<FrameworkContext> context);
    std::vector<std::shared_ptr<PipelineContext>> &contexts();
    void bindTexture(std::shared_ptr<gpu::Sampler> sampler, std::shared_ptr<gpu::DeviceLocalImage> image, int index);

    std::shared_ptr<WorldPipeline> worldPipeline();

    std::shared_ptr<WorldPipelineBlueprint> worldPipelineBlueprint();

    bool isRecreationNeeded = false;

  private:
    void createContexts(const std::shared_ptr<Framework> &framework);

    static std::atomic<bool> nativeRebuildActive_;

    std::weak_ptr<Framework> framework_;

    std::shared_ptr<WorldPipeline> worldPipeline_;

    std::shared_ptr<WorldPipelineBlueprint> worldPipelineBlueprint_;

    // Shared so that a rebuild can retire the old set whole while frames in flight may still use it.
    std::shared_ptr<std::vector<std::shared_ptr<PipelineContext>>> contexts_;

    // Last, so it is destroyed (its jobs joined) first: they hold objects of the world pipeline.
    ShaderPrewarm shaderPrewarm_;
};

// What the framework's frame slot runs of the pipeline.
struct PipelineContext : public SharedObject<PipelineContext> {
    std::weak_ptr<FrameworkContext> frameworkContext;

    // Null while there is no world pipeline.
    std::shared_ptr<WorldPipelineContext> worldPipelineContext;

    PipelineContext(std::shared_ptr<FrameworkContext> context, std::shared_ptr<Pipeline> pipeline);
};
