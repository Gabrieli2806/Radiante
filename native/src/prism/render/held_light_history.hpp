#pragma once

#include "common/shared.hpp"

#include <algorithm>
#include <cmath>

namespace held_light_history {

inline glm::vec4 signature(const glm::vec4 &position, const glm::vec4 &colour, float brightness) {
    if (!(colour.w > 0.0f && position.w > 0.0f && brightness > 0.0f)) return glm::vec4(0.0f);
    const glm::vec3 radiance = glm::max(glm::vec3(colour) * brightness, glm::vec3(0.0f));
    if (!(glm::max(radiance.x, glm::max(radiance.y, radiance.z)) > 0.0f)) return glm::vec4(0.0f);
    return glm::vec4(radiance, position.w);
}

inline bool different(const glm::vec4 &a, const glm::vec4 &b) {
    for (int i = 0; i < 4; ++i) {
        const float tolerance = 1e-5f * std::max({1.0f, std::abs(a[i]), std::abs(b[i])});
        if (std::abs(a[i] - b[i]) > tolerance) return true;
    }
    return false;
}

inline bool resetFrame(bool sourceChanged, uint32_t framesInFlight, uint32_t &remaining) {
    if (sourceChanged) remaining = std::max(framesInFlight, 1u);
    if (remaining == 0) return false;
    --remaining;
    return true;
}

// Light motion is continuous. A changed source invalidates lighting history even when the surfaces did not move.
inline bool changed(const gpu::Data::WorldUBO &now, const gpu::Data::WorldUBO &previous) {
    return different(signature(now.heldLightPos, now.heldLightColor, now.heldLightBrightness),
                     signature(previous.heldLightPos, previous.heldLightColor, previous.heldLightBrightness)) ||
           different(signature(now.offHandLightPos, now.offHandLightColor, now.heldLightBrightness),
                     signature(previous.offHandLightPos, previous.offHandLightColor, previous.heldLightBrightness));
}

} // namespace held_light_history
