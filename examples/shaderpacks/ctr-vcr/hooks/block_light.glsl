#ifndef RADIANTE_HOOK_BLOCK_LIGHT_GLSL
#define RADIANTE_HOOK_BLOCK_LIGHT_GLSL

// CTR VCR (Radiante port): the warm cast of block light under an old tube, CTR VCR's COLOR_LIGHT_BLOCK (1.00, 0.91,
// 0.84) and COLOR_LIGHT_HAND (1.02, 0.83, 0.70) from shaders/shader.h (Apache-2.0, LICENSE-CTRVCR), blended in by
// CTRVCR_WARM_LIGHT so the light keeps its strength. The glow of the flame itself is left as it is.
#ifndef CTRVCR_WARM_LIGHT
#    define CTRVCR_WARM_LIGHT 1.0
#endif

#include "common/block_light_hook.glsl"

void hookBlockLight(inout vec3 radiance, uint kind) {
    if (kind == RADIANTE_BLOCK_LIGHT_GLOW) { return; }
    vec3 tint = kind == RADIANTE_BLOCK_LIGHT_HELD ? vec3(1.02, 0.83, 0.70) : vec3(1.0, 0.91, 0.84);
    radiance *= mix(vec3(1.0), tint, float(CTRVCR_WARM_LIGHT));
}

#endif
