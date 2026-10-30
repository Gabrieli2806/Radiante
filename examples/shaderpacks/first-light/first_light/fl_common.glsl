#ifndef FIRST_LIGHT_COMMON_GLSL
#define FIRST_LIGHT_COMMON_GLSL

// First Light: what the hooks share. Every setting is a live one ("runtime": true in configs.json): its define reads
// the world uniform, so it is tested with if, never #if. The defaults in the hooks are only for a compile outside the
// loader, which always defines them. The weather and the time of day come from radiante-pt's common/pack_world.glsl.

#include "common/pack_world.glsl"

const vec3 FL_LUMA = vec3(0.2126, 0.7152, 0.0722);

float flLuminance(vec3 c) {
    return dot(c, FL_LUMA);
}

// Saturation about the luminance, which it keeps. Clamped at zero so a strong push cannot make a channel negative.
vec3 flSaturate(vec3 c, float amount) {
    float l = flLuminance(c);
    return max(mix(vec3(l), c, amount), vec3(0.0));
}

#endif
