#ifndef RADIANTE_HOOK_BLOCK_LIGHT_GLSL
#define RADIANTE_HOOK_BLOCK_LIGHT_GLSL

// First Light: the vivid emissive light of the showcases, where glowstone, sea lanterns and lava were the night's main
// light and painted the walls round them in their own colour. Every block light gets more saturated (about its
// luminance, which is kept) and FL_LIGHT_STRENGTH brighter. The same change for every kind, so the lamp's face, the
// room it lights and the haze round it agree, and a glow a bounce ray meets is as bright as the light it stands for.
#ifndef FL_LIGHT_COLOUR
#    define FL_LIGHT_COLOUR 0.5
#endif
#ifndef FL_LIGHT_STRENGTH
#    define FL_LIGHT_STRENGTH 1.3
#endif

#include "first_light/fl_common.glsl"

void hookBlockLight(inout vec3 radiance, uint kind) {
    float colour = clamp(float(FL_LIGHT_COLOUR), 0.0, 1.0);
    if (colour > 0.0) { radiance = flSaturate(radiance, 1.0 + 0.8 * colour); }
    radiance *= max(float(FL_LIGHT_STRENGTH), 0.0);
}

#endif
