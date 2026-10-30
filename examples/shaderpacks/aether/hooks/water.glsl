#ifndef RADIANTE_HOOK_WATER_GLSL
#define RADIANTE_HOOK_WATER_GLSL

// Aether: clearer, turquoise water. Turquoise Water reshapes how the water absorbs light: red goes within a few
// blocks, green last and blue nearly as late, so shallows read clear aqua and depth turns teal rather than navy; the
// total stays Radiante PT's, so only the tint changes. Water Clarity then thins it all, up to two fifths, so the
// bottom shows through deeper water. The light the water scatters takes the same tint (radiante-pt scatters a share
// of the extinction).
#include "aether/aether_common.glsl"

void hookWaterAbsorption(inout vec3 extinction) {
    float tint = clamp(float(AETHER_WATER_TINT), 0.0, 1.0);
    // -log of each colour's share in a turquoise of (0.10, 0.80, 0.72), plus a floor: red ~2.2, green ~0.1, blue ~0.2.
    const vec3 shape = vec3(2.18, 0.10, 0.205);
    float total = extinction.r + extinction.g + extinction.b;
    vec3 turquoise = shape * (total / (shape.r + shape.g + shape.b));
    extinction = mix(extinction, turquoise, tint);
    extinction *= mix(1.0, 0.6, clamp(float(AETHER_WATER_CLARITY), 0.0, 1.0));
}

#endif
