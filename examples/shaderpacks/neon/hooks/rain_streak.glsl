#ifndef RADIANTE_HOOK_RAIN_STREAK_GLSL
#define RADIANTE_HOOK_RAIN_STREAK_GLSL

#include "neon/neon_common.glsl"

// Neon Dusk: the rain glows, each column of it pink or cyan as the signs of a street at night would light it, so a
// storm reads as falling neon. The streak's light is its own colour at the brightness of radiante-pt's glint (the sky
// above it) with a floor that keeps it seen at night, and what it lets through takes a little of that colour too.
// NEON_WEATHER (live, neon/neon_common.glsl) sets how much: 0 leaves radiante-pt's grey rain.
//
// The colour goes by the block column the streak falls in (its absolute x and z), so a streak keeps its colour from
// frame to frame and in every ray that sees it.
void hookRainStreak(inout vec3 light, inout vec3 tint, float coverage, vec3 position) {
    float amount = clamp(float(NEON_WEATHER), 0.0, 1.0);
    if (amount <= 0.0) { return; }
    dvec2 absolute = dvec2(worldUBO.cameraPos.xz) + dvec2(position.xz);
    vec3 colour = neonWeatherColour(ivec2(floor(absolute)));
    float glint = max(neonLuminance(light) * 1.8, 0.04 * coverage);
    light = mix(light, colour * glint, amount);
    tint = mix(tint, tint * mix(vec3(1.0), colour, 0.35 * coverage), amount);
}

#endif
