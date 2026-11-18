#pragma once

// Shared helpers between shader_pack_loader.cpp and shader_pack_loader_passes.cpp. Not for use outside this folder.

#include "prism/render/modules/world/shader_pack/shader_pack_internal.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace shader_pack::loader {

using json = nlohmann::json;

struct HitShaderFiles {
    std::optional<fs::path> anyHit;
    std::optional<fs::path> closestHit;
    std::optional<fs::path> intersection;
};
using HitShadersByGroup = std::unordered_map<std::string, HitShaderFiles>;

[[noreturn]] void fail(const std::string &message);
std::string at(const std::string &context, std::string_view key);
std::string at(const std::string &context, size_t index);
const json *field(const json &object, std::string_view key);
const json &requireObject(const json &object, std::string_view key, const std::string &context);
const json &requireArray(const json &object, std::string_view key, const std::string &context);
std::string requireString(const json &object, std::string_view key, const std::string &context);
std::optional<std::string> optionalString(const json &object, std::string_view key, const std::string &context);
std::optional<bool> optionalFlag(const json &object, std::string_view key, const std::string &context);
bool optionalBool(const json &object, std::string_view key, bool fallback, const std::string &context);
std::optional<uint32_t> optionalUnsigned(const json &object, std::string_view key, const std::string &context);
std::vector<std::string> stringArray(const json &object, std::string_view key, const std::string &context);
std::string expressionText(const json &value, const std::string &context);
void readExpression(const json &object, std::string_view key, const std::string &context, std::string &expression);
std::string scalarText(const json &value, const std::string &context);
std::optional<std::string> inferScalarType(const json &value, bool allowString);
fs::path packFile(const fs::path &root, const std::string &reference, const std::string &context);
fs::path requireFile(const json &object, std::string_view key, const fs::path &root, const std::string &context);
std::optional<fs::path>
optionalFile(const json &object, std::string_view key, const fs::path &root, const std::string &context);

ShaderPackLoader::Stage parseStage(const json &value, const std::string &context);
std::unordered_map<std::string, std::string> parseDefinitions(const json &object, const std::string &context);
ShaderPackLoader::VariableConfig parseVariable(const json &object, const std::string &context);

HitShadersByGroup findHitShaders(const fs::path &root);
ShaderPackLoader::PassConfig
parsePass(const json &pass, const fs::path &root, const HitShadersByGroup &hitShaders, const std::string &context);
const std::string &passName(const ShaderPackLoader::PassConfig &pass);
ShaderPackLoader::ExecutionConfig parseExecution(const json &object, const std::string &context);

} // namespace shader_pack::loader
