#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

class Framework;

namespace shader_pack {

// What checkPack found. `error` is why the pack did not load (configs.json, extends, a missing file); `errors` holds
// one entry per shader that does not compile.
struct PackCheck {
    bool ok = false;
    std::string error;
    std::vector<std::string> errors;
    size_t shaders = 0;
    size_t compiled = 0;
};

// Loads a pack and compiles every shader a build of it would, for both stages, with the ray tracing module's
// attributes (`attributeKVs`, the pack's path among them) and this device's features, without touching the running
// pipeline: no fallback to the built-in pack, extraction and staging in a folder of the check's own, no shader
// modules, nothing in the error log. For /radiante pack validate and the hot reload of folder packs. Thread-safe;
// checks run one at a time.
// `micromaps`, when given, stands for the device's opacity micromaps (VPT_OMM_SUPPORTED): the SPIR-V bundle compiles
// both without a device (spirv_bundle).
PackCheck checkPack(const std::shared_ptr<Framework> &framework,
                    const std::vector<std::string> &attributeKVs,
                    const std::string &language,
                    std::optional<bool> micromaps = std::nullopt);

} // namespace shader_pack
