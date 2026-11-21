// The SPIR-V bundle's compile, for the spirv_bundle tool (src/tools/spirv_bundle.cpp), which loads this library and
// calls it when Radiante is built. Not used by the game.

#include "prism/render/modules/world/shader_pack/shader_pack.hpp"
#include "prism/render/modules/world/shader_pack/shader_pack_check.hpp"
#include "prism/render/renderer.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#    define PRISM_BUNDLE_EXPORT extern "C" __declspec(dllexport)
#else
#    define PRISM_BUNDLE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

int fail(char *message, int messageSize, const std::string &text) {
    if (message != nullptr && messageSize > 0) {
        std::strncpy(message, text.c_str(), static_cast<size_t>(messageSize) - 1);
        message[messageSize - 1] = '\0';
    }
    return -1;
}

} // namespace

// Compiles radiante-pt, as a build of it would with each variant's settings, into <rendererFolder>/cache/shaders: the
// game's own SPIR-V cache, files named as the game names them. rendererFolder holds the renderer's shaders as the game
// unpacks them (shaders/include, shaders/world/ray_tracing/radiante-pt.zip). variantsPath is a JSON list of
// {"name", "micromaps": bool, "attributes": {"<full attribute name>": "<value>"}}; an attribute not given keeps the
// pack's default, as on a first start. Returns how many shaders were compiled, or -1 with the reason in message.
PRISM_BUNDLE_EXPORT int prismrtCompileSpirvBundle(const char *rendererFolder,
                                                  const char *variantsPath,
                                                  char *message,
                                                  int messageSize) {
    try {
        Renderer::folderPath = rendererFolder;
        std::ifstream stream(variantsPath);
        if (!stream) return fail(message, messageSize, std::string("cannot read ") + variantsPath);
        nlohmann::json variants;
        stream >> variants;
        if (!variants.is_array()) return fail(message, messageSize, "the variants are not a list");
        int compiled = 0;
        for (const auto &variant : variants) {
            const std::string name = variant.value("name", std::string("?"));
            std::vector<std::string> attributes;
            if (variant.contains("attributes")) {
                for (const auto &[key, value] : variant["attributes"].items()) {
                    attributes.push_back(key);
                    attributes.push_back(value.get<std::string>());
                }
            }
            attributes.push_back("render_pipeline.module.ray_tracing.attribute.shader_pack_path");
            attributes.push_back(ShaderPack::builtInShaderPackPath().string());
            const shader_pack::PackCheck check =
                shader_pack::checkPack(nullptr, attributes, "en_us", variant.value("micromaps", false));
            if (!check.ok) {
                std::string reason = check.error;
                for (const std::string &error : check.errors) reason += "\n" + error;
                return fail(message, messageSize, "variant " + name + ": " + reason);
            }
            compiled += static_cast<int>(check.compiled);
        }
        return compiled;
    } catch (const std::exception &exception) {
        return fail(message, messageSize, exception.what());
    }
}
