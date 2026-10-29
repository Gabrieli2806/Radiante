#ifndef RADIANTE_HOOK_UNDERWATER_FOG_GLSL
#define RADIANTE_HOOK_UNDERWATER_FOG_GLSL

// Aether: under water the view fades into a blue-green at the brightness of Radiante PT's own colour there (so the
// light under water stays as bright), by Turquoise Water, and reaches further with Water Clarity (up to 1.6 times).
// The ripple of the picture itself is hooks/underwater.glsl's.
#include "aether/aether_common.glsl"

void hookUnderwaterFog(inout vec3 colour, inout float visibility) {
    float tint = clamp(float(AETHER_WATER_TINT), 0.0, 1.0);
    colour = mix(colour, aetherAtLuma(vec3(0.06, 0.45, 0.48), aetherLuma(colour)), 0.75 * tint);
    visibility *= mix(1.0, 1.6, clamp(float(AETHER_WATER_CLARITY), 0.0, 1.0));
}

#endif
