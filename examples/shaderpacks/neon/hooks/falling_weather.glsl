#ifndef RADIANTE_HOOK_FALLING_WEATHER_GLSL
#define RADIANTE_HOOK_FALLING_WEATHER_GLSL

#include "neon/neon_common.glsl"

// Neon Dusk: the weather glows. Each column of rain is pink or cyan, as the signs of a street at night would light
// it, so a storm reads as falling neon, and snowflakes shine in both colours, column by column. The light is the
// colour at the brightness of radiante-pt's own (the sky above) with a floor that keeps it seen at night; what rain
// lets through takes a little of that colour too. NEON_WEATHER (live, neon/neon_common.glsl) sets how much: 0 leaves
// radiante-pt's weather.
//
// The colour goes by the block column the streak or the flake falls in, so it keeps its colour as it falls and in
// every ray that sees it.
void hookFallingWeather(inout vec3 light, inout vec3 tint, float coverage, vec3 position, bool snow) {
    float amount = clamp(float(NEON_WEATHER), 0.0, 1.0);
    if (amount <= 0.0) { return; }
    dvec3 absolute = dvec3(worldUBO.cameraPos) + dvec3(position);
    if (snow) {
        vec3 colour = neonWeatherColour(ivec2(floor(absolute.xz)) + ivec2(7919, 104729));
        float glow = max(neonLuminance(light) * 2.0, 0.06 * coverage);
        light = mix(light, colour * glow, amount);
        return;
    }
    vec3 colour = neonWeatherColour(ivec2(floor(absolute.xz)));
    float glint = max(neonLuminance(light) * 1.8, 0.03 * coverage * coverage);
    light = mix(light, colour * glint, amount);
    tint = mix(tint, tint * mix(vec3(1.0), colour, 0.35 * coverage), amount);
}

#endif
