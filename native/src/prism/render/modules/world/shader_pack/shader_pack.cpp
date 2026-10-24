#include "prism/render/modules/world/shader_pack/shader_pack_internal.hpp"

#include "prism/render/renderer.hpp"
#include "prism/util/logging.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

ShaderPack::ShaderPack(std::shared_ptr<Framework> framework) : framework_(framework) {}

std::filesystem::path ShaderPack::builtInShaderPackPath() {
    return Renderer::folderPath / "shaders/world/ray_tracing/radiante-pt.zip";
}

ShaderPack::BuildConfig ShaderPack::buildConfigFromRayTracingAttributes(const std::vector<std::string> &attributeKVs) {
    BuildConfig config;
    for (size_t i = 0; i + 1 < attributeKVs.size(); i += 2) {
        const std::string &key = attributeKVs[i];
        const std::string &value = attributeKVs[i + 1];
        config.staticAttributes[key] = value;
        if (key == "render_pipeline.module.ray_tracing.attribute.shader_pack_path") {
            config.shaderPackPath = value;
        } else if (key == "render_pipeline.module.ray_tracing.attribute.use_sharc") {
            config.shouldUseSharc = value == "render_pipeline.true";
        }
    }
    return config;
}

bool ShaderPack::initialize(const BuildConfig &config, std::string &error) {
    const fs::path builtIn = builtInShaderPackPath();
    ShaderPackLoader::LoadResult loaded = ShaderPackLoader::load(config.shaderPackPath, builtIn, config.language);
    if (loaded.success && loaded.shaderPack.requiresEmission && !Renderer::options.collectChunkEmission) {
        loaded = ShaderPackLoader::load(builtIn, builtIn, config.language);
    }
    if (!loaded.success) {
        error = loaded.error;
        return false;
    }

    shaderPack_ = std::move(loaded.shaderPack);
    staticAttributeValues_ = config.staticAttributes;
    shaderAttributes_ = ShaderPackLoader::buildAttributeDefinitions(shaderPack_.attributes, staticAttributeValues_);

    // What the runtime attributes start at in WorldUBO.packParams: the values this build was asked for.
    runtimeValues_.fill(glm::vec4(0.0f));
    for (const auto &attribute : shaderPack_.attributes) {
        if (!attribute.runtime) continue;
        std::optional<glm::vec4> value;
        if (auto requested = staticAttributeValues_.find(attribute.name); requested != staticAttributeValues_.end()) {
            value = ShaderPackLoader::parseRuntimeValue(attribute.type, requested->second);
            if (!value.has_value()) {
                // Compiled in, this value would have failed the shader build; the default keeps the GPU and the
                // execution script (which reads the values kept here) in agreement instead.
                radiante::err() << "[ShaderPack] " << attribute.name << ": cannot use \"" << requested->second
                                << "\", using the default " << attribute.defaultValue << std::endl;
                requested->second = attribute.defaultValue;
            }
        }
        if (!value.has_value()) value = ShaderPackLoader::parseRuntimeValue(attribute.type, attribute.defaultValue);
        runtimeValues_[attribute.runtimeSlot] = value.value_or(glm::vec4(0.0f));
    }
    rebuildAttributeNumbers();

    initStageRuntime(rayTracingStageRuntime_, shaderPack_.rayTracingExecution);
    initStageRuntime(postRenderStageRuntime_, shaderPack_.postRenderExecution);
    hasSharcRuntime_ = config.shouldUseSharc && shaderPack_.sharc.has_value();
    return true;
}

void ShaderPack::rebuildAttributeNumbers() {
    attributeNumbers_.clear();
    attributeNumbersError_ = nullptr;

    std::vector<std::pair<std::string, double>> numbers;
    try {
        numbers = shader_pack::attributeNumbers(shaderPack_.attributes, staticAttributeValues_);
    } catch (...) {
        attributeNumbersError_ = std::current_exception();
        return;
    }
    std::unordered_set<std::string> names;
    for (auto &[name, value] : numbers) {
        if (names.insert(name).second) attributeNumbers_.push_back({.name = std::move(name), .value = value});
    }
}

const ShaderPackLoader::ShaderPack &ShaderPack::shaderPack() const {
    return shaderPack_;
}

const std::unordered_map<std::string, std::string> &ShaderPack::shaderAttributes() const {
    return shaderAttributes_;
}

bool ShaderPack::hasSharcRuntime() const {
    return hasSharcRuntime_;
}

const std::array<glm::vec4, PRISM_PACK_PARAM_COUNT> &ShaderPack::runtimeValues() const {
    return runtimeValues_;
}

std::optional<uint32_t> ShaderPack::runtimeSlot(const std::string &name) const {
    for (const auto &attribute : shaderPack_.attributes) {
        if (attribute.name != name) continue;
        if (!attribute.runtime) return std::nullopt;
        return attribute.runtimeSlot;
    }
    return std::nullopt;
}

bool ShaderPack::setRuntimeAttribute(const std::string &name,
                                     const std::string &value,
                                     uint32_t &slot,
                                     glm::vec4 &packed) {
    const auto attribute = std::find_if(shaderPack_.attributes.begin(), shaderPack_.attributes.end(),
                                        [&](const auto &candidate) { return candidate.name == name; });
    if (attribute == shaderPack_.attributes.end() || !attribute->runtime) return false;
    const auto parsed = ShaderPackLoader::parseRuntimeValue(attribute->type, value);
    if (!parsed.has_value()) return false;

    // The execution script's conditions and the dispatch sizes read the attributes from these values every frame.
    staticAttributeValues_[name] = value;
    rebuildAttributeNumbers();
    runtimeValues_[attribute->runtimeSlot] = *parsed;
    slot = attribute->runtimeSlot;
    packed = *parsed;
    return true;
}

void ShaderPack::initStageRuntime(StageRuntime &stageRuntime, const ShaderPackLoader::ExecutionConfig &execution) {
    stageRuntime.executionVariableConfigs.clear();
    stageRuntime.globalVariables.clear();
    for (const auto &variable : execution.globalVariables) {
        stageRuntime.globalVariables[variable.name] = variable.defaultValue;
    }
    for (const auto &variable : execution.variables) stageRuntime.executionVariableConfigs[variable.name] = variable;
}

const ShaderPack::StageRuntime &ShaderPack::stageRuntime(ShaderPackLoader::Stage stage) const {
    return stage == ShaderPackLoader::Stage::PostRender ? postRenderStageRuntime_ : rayTracingStageRuntime_;
}

const ShaderPackLoader::ExecutionConfig &ShaderPack::execution(ShaderPackLoader::Stage stage) const {
    return stage == ShaderPackLoader::Stage::PostRender ? shaderPack_.postRenderExecution
                                                        : shaderPack_.rayTracingExecution;
}

std::string ShaderPack::executionSource(ShaderPackLoader::Stage stage, uint32_t executionSet) const {
    return ShaderPackLoader::buildExecutionSource(execution(stage).variables, executionSet);
}

void ShaderPack::copyStageExecutionState(
    ShaderPackLoader::Stage stage,
    std::unordered_map<std::string, ShaderPackLoader::VariableConfig> &variableConfigs,
    std::unordered_map<std::string, std::string> &globalVariables) const {
    const StageRuntime &runtime = stageRuntime(stage);
    variableConfigs = runtime.executionVariableConfigs;
    globalVariables = runtime.globalVariables;
}
