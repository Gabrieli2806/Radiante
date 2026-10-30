#ifndef RADIANTE_HOOK_DIRECT_LIGHT_GLSL
#define RADIANTE_HOOK_DIRECT_LIGHT_GLSL

// Neon Dusk: sunlight and moonlight in the look's colours. The sun is a faintly pink white by day and a hot peach
// pink at dusk, and a violet blue in the night look; the moon is always a cool cyan. Locked, the sun takes the locked
// look's colour all day (a locked dusk has dusk-coloured sunlight at noon). Only the hue changes: the tint has a
// luminance of 1, so the light keeps Radiante's strength, direction, shadows and falloff.
#ifndef NEON_SUN_TINT
#    define NEON_SUN_TINT 0.35
#endif

#include "neon/neon_common.glsl"

vec3 neonHueOf(vec3 c) {
    return c / dot(c, NEON_LUMA);
}

void hookDirectLight(SampledSurface surface, vec3 viewDir, vec3 lightDir, float NoL, inout vec3 lightBRDF) {
    float tint = clamp(float(NEON_SUN_TINT), 0.0, 1.0);
    if (tint <= 0.0) { return; }
    bool moon = dot(lightDir, radianteMoonDirection()) > dot(lightDir, radianteSunDirection());
    vec3 hue;
    if (moon) {
        hue = neonHueOf(vec3(0.6, 0.8, 1.0));
    } else {
        vec3 w = neonLookWeights();
        hue = w.x * neonHueOf(vec3(1.0, 0.93, 0.97)) + w.y * neonHueOf(vec3(1.0, 0.58, 0.6)) +
              w.z * neonHueOf(vec3(0.62, 0.62, 1.0));
    }
    lightBRDF *= mix(vec3(1.0), hue, tint);
}

#endif
