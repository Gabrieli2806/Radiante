#include "prism/render/modules/world/shader_pack/shader_pack_check.hpp"

#include "prism/render/modules/world/shader_pack/shader_pack_internal.hpp"
#include "prism/render/render_framework.hpp"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace shader_pack {

namespace {

// The runtime resources' descriptor set in each stage (RayTracingModule's and PostRenderModule's
// kRuntimeResourceSet); the execution variables take the set after it. The requests must name the same sets as a
// build's, or their SPIR-V would differ from what the build compiles.
constexpr uint32_t kRayTracingRuntimeResourceSet = 5;
constexpr uint32_t kPostRenderRuntimeResourceSet = 4;
// Where a check extracts zips and stages packs, under temp/shaders/world/ray_tracing/.
constexpr char kCheckFolder[] = "check";

// What a post render pass of this content draws (PostRenderModule's drawsContent): the stars only; particles, weather
// and text are traced in the world, so their passes compile nothing.
bool drawsContent(const std::string &content) {
    const std::string name = ShaderPackLoader::toLower(content);
    if (name == "star" || name == "stars") return true;
    for (std::string_view traced : {"weather", "particle", "particles", "text", "name_tag", "nametag", "name-tag"}) {
        if (name == traced) return false;
    }
    throw std::runtime_error("unsupported post_render render content: " + content);
}

ShaderPack::ShaderCreateInfo request(const fs::path &path,
                                     VkShaderStageFlagBits stage,
                                     std::unordered_map<std::string, std::string> definitions,
                                     ShaderPackLoader::Stage executionStage,
                                     uint32_t executionSet) {
    return {
        .path = path,
        .stage = stage,
        .definitions = std::move(definitions),
        .executionStage = executionStage,
        .executionSet = executionSet,
    };
}

// The shaders RayTracingModule::collectShaders asks for, pass by pass.
void collectRayTracing(const ShaderPack &pack,
                       Framework *framework,
                       std::vector<ShaderPack::ShaderCreateInfo> &requests) {
    const ShaderPackLoader::ShaderPack &config = pack.shaderPack();
    const uint32_t executionSet = pack.executionSet(kRayTracingRuntimeResourceSet);
    constexpr auto stage = ShaderPackLoader::Stage::RayTracing;
    const bool micromaps = framework != nullptr && framework->device()->isOpacityMicromapEnabled();
    const bool reordering = framework != nullptr && framework->device()->isShaderExecutionReorderingEnabled();

    for (const auto &pass : config.passes) {
        if (pass.stage != stage) continue;
        switch (pass.type) {
            case ShaderPackLoader::PassConfig::Type::FullScreen: {
                const auto &fullScreen = pass.fullScreen;
                // A cube target named without a face or layer is drawn face by face, with a pipeline each.
                const bool cube = std::any_of(config.textures.begin(), config.textures.end(), [&](const auto &texture) {
                    return texture.name == fullScreen.target &&
                           texture.dimension == ShaderPackLoader::TextureDimension::Cube;
                });
                const uint32_t pipelines =
                    cube && !fullScreen.face.has_value() && !fullScreen.layer.has_value() ? 6u : 1u;
                for (uint32_t i = 0; i < pipelines; i++) {
                    auto definitions = fullScreen.definitions;
                    if (cube) definitions["FACE"] = std::to_string(fullScreen.face.value_or(i));
                    requests.push_back(request(fullScreen.fragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT,
                                               std::move(definitions), stage, executionSet));
                }
                break;
            }
            case ShaderPackLoader::PassConfig::Type::Compute:
                requests.push_back(request(pass.compute.computeShaderPath, VK_SHADER_STAGE_COMPUTE_BIT,
                                           pass.compute.definitions, stage, executionSet));
                break;
            case ShaderPackLoader::PassConfig::Type::Render:
                throw std::runtime_error("render pass is only supported by post_render stage: " + pass.render.name);
            case ShaderPackLoader::PassConfig::Type::RayTracing: {
                const auto &rayTracing = pass.rayTracing;
                auto definitions = rayTracing.definitions;
                if (std::getenv("RADIANTE_NO_SOBOL") != nullptr) definitions["VPT_LOW_DISCREPANCY"] = "0";
                definitions["VPT_OMM_SUPPORTED"] = micromaps ? "1" : "0";
                for (const auto &miss : rayTracing.missShaders) {
                    requests.push_back(
                        request(miss.shaderPath, VK_SHADER_STAGE_MISS_BIT_KHR, definitions, stage, executionSet));
                }
                for (const auto &group : rayTracing.hitGroups) {
                    if (group.closestHit.has_value()) {
                        requests.push_back(request(*group.closestHit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, definitions,
                                                   stage, executionSet));
                    }
                    if (group.anyHit.has_value()) {
                        requests.push_back(
                            request(*group.anyHit, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, definitions, stage, executionSet));
                    }
                    if (group.intersection.has_value()) {
                        requests.push_back(request(*group.intersection, VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
                                                   definitions, stage, executionSet));
                    }
                }
                auto rayGen = definitions;
                rayGen["VPT_SER_SUPPORTED"] = reordering ? "1" : "0";
                auto query = rayGen;
                if (pack.hasSharcRuntime() && rayTracing.querySharc) {
                    query["USE_SHARC"] = "1";
                    query["SHARC_QUERY"] = "1";
                }
                requests.push_back(
                    request(rayTracing.rayGenShaderPath, VK_SHADER_STAGE_RAYGEN_BIT_KHR, query, stage, executionSet));
                if (pack.hasSharcRuntime() && config.sharc->updatePassName == rayTracing.name) {
                    rayGen["USE_SHARC"] = "1";
                    rayGen["SHARC_UPDATE"] = "1";
                    requests.push_back(request(rayTracing.rayGenShaderPath, VK_SHADER_STAGE_RAYGEN_BIT_KHR, rayGen,
                                               stage, executionSet));
                    requests.push_back(request(config.sharc->resolveCompShaderPath, VK_SHADER_STAGE_COMPUTE_BIT,
                                               rayGen, stage, executionSet));
                }
                break;
            }
        }
    }
}

// The shaders PostRenderModule::ensureDynamicPipelines asks for.
void collectPostRender(const ShaderPack &pack, std::vector<ShaderPack::ShaderCreateInfo> &requests) {
    const uint32_t executionSet = pack.executionSet(kPostRenderRuntimeResourceSet);
    constexpr auto stage = ShaderPackLoader::Stage::PostRender;
    for (const auto &pass : pack.shaderPack().passes) {
        if (pass.stage != stage) continue;
        switch (pass.type) {
            case ShaderPackLoader::PassConfig::Type::FullScreen:
                requests.push_back(request(pass.fullScreen.fragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT,
                                           pass.fullScreen.definitions, stage, executionSet));
                break;
            case ShaderPackLoader::PassConfig::Type::Render: {
                if (!drawsContent(pass.render.content)) break;
                const auto shaders = pass.render.shaderConfigs.find("default");
                if (shaders == pass.render.shaderConfigs.end()) break;
                requests.push_back(request(shaders->second.vertexShaderPath, VK_SHADER_STAGE_VERTEX_BIT,
                                           pass.render.definitions, stage, executionSet));
                requests.push_back(request(shaders->second.fragmentShaderPath, VK_SHADER_STAGE_FRAGMENT_BIT,
                                           pass.render.definitions, stage, executionSet));
                break;
            }
            case ShaderPackLoader::PassConfig::Type::RayTracing:
            case ShaderPackLoader::PassConfig::Type::Compute:
                throw std::runtime_error("post_render stage only supports full_screen and render passes");
        }
    }
}

} // namespace

PackCheck checkPack(const std::shared_ptr<Framework> &framework,
                    const std::vector<std::string> &attributeKVs,
                    const std::string &language) {
    // The check folder is shared by every check.
    static std::mutex checking;
    std::scoped_lock lock(checking);

    PackCheck check;
    ShaderPack::BuildConfig config = ShaderPack::buildConfigFromRayTracingAttributes(attributeKVs);
    config.language = language;
    ShaderPack pack(framework);
    try {
        if (!pack.initialize(config, check.error, {.fallBack = false, .tempFolder = kCheckFolder})) return check;
        std::vector<ShaderPack::ShaderCreateInfo> requests;
        collectRayTracing(pack, framework.get(), requests);
        collectPostRender(pack, requests);
        ShaderPack::CheckResult result = pack.checkShaders(requests, pack.shaderAttributes());
        check.shaders = result.shaders;
        check.compiled = result.compiled;
        check.errors = std::move(result.errors);
    } catch (const std::exception &exception) {
        check.error = exception.what();
        return check;
    }
    check.ok = check.error.empty() && check.errors.empty();
    return check;
}

} // namespace shader_pack
