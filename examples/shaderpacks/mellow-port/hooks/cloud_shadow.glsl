#ifndef RADIANTE_HOOK_CLOUD_SHADOW_GLSL
#define RADIANTE_HOOK_CLOUD_SHADOW_GLSL

// Mellow port: the shadows of Mellow's clouds on the world. Mellow itself casts none (its shadow map holds only the
// world); these come from the same clouds the sky shows (mellowCloudShadow in mellow/mellow_clouds.glsl), so a cloud
// passing over the sun darkens the ground under it. Cloud Shadows sets how dark: 0 leaves the sunlight as it was.
#ifndef MELLOW_CLOUD_SHADOWS
#    define MELLOW_CLOUD_SHADOWS 0.6
#endif

#include "mellow/mellow_clouds.glsl"

float hookCloudShadow(vec3 worldPos, vec3 lightDir) {
#if MELLOW_CLOUD_STYLE == 0
    return 1.0;
#else
    if (float(MELLOW_CLOUD_SHADOWS) <= 0.0) return 1.0;
    return mix(1.0, mellowCloudShadow(worldPos, lightDir), float(MELLOW_CLOUD_SHADOWS));
#endif
}

#endif
