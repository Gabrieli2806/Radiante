#ifndef RADIANTE_HOOK_CLOUD_SHADOW_GLSL
#define RADIANTE_HOOK_CLOUD_SHADOW_GLSL

// Aether: the shadows of the pack's cumulus on the world, drifting with them: the cloud the sky shows where the sun
// (or moon) shines through it (aetherCloudShadow in aether/aether_cloud_shape.glsl). Cloud Shadows sets how dark,
// 0 leaving the light as it was. The cirrus is too thin to cast one. Each light sample is aimed at its own point of
// the sun's disc, so the shadows' edges come out soft, as under real clouds.
#include "aether/aether_cloud_shape.glsl"

float hookCloudShadow(vec3 worldPos, vec3 lightDir) {
#if AETHER_CLOUDS
    float strength = clamp(float(AETHER_CLOUD_SHADOWS), 0.0, 1.0);
    // Only the overworld has these clouds; in the End this light is the End flash's.
    if (strength <= 0.0 || worldUBO.skyType != 1u) { return 1.0; }
    return mix(1.0, aetherCloudShadow(worldPos, lightDir), strength);
#else
    return 1.0;
#endif
}

#endif
