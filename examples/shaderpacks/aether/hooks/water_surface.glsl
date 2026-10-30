#ifndef RADIANTE_HOOK_WATER_SURFACE_GLSL
#define RADIANTE_HOOK_WATER_SURFACE_GLSL

// Aether: the water's surface pulled towards turquoise and made glassier. Its colour (the biome's water colour, which
// tints the surface layer) moves part of the way towards a turquoise of the same brightness with Turquoise Water, so
// the surface agrees with the water under it (hooks/water.glsl); Calm Water makes it smoother, down to three fifths
// of its roughness, so the sky and the sun's glitter mirror more cleanly. Radiante PT's waves still bend it.
#include "aether/aether_common.glsl"

void hookWaterSurface(inout SampledSurface surface, vec3 position, float waveHeight) {
    float tint = clamp(float(AETHER_WATER_TINT), 0.0, 1.0) * 0.4;
    const vec3 turquoise = vec3(0.10, 0.62, 0.60);
    vec3 albedo = min(aetherAtLuma(turquoise, aetherLuma(surface.mat.albedo)), vec3(1.0));
    vec3 value = min(aetherAtLuma(turquoise, aetherLuma(surface.albedoValue.rgb)), vec3(1.0));
    surface.mat.albedo = mix(surface.mat.albedo, albedo, tint);
    surface.albedoValue.rgb = mix(surface.albedoValue.rgb, value, tint);
    surface.mat.roughness *= mix(1.0, 0.6, clamp(float(AETHER_WATER_CALM), 0.0, 1.0));
}

#endif
