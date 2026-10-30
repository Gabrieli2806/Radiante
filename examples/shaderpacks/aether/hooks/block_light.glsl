#ifndef RADIANTE_HOOK_BLOCK_LIGHT_GLSL
#define RADIANTE_HOOK_BLOCK_LIGHT_GLSL

// Aether: candle-warm flames and colder soul fire. There is no block id, so lights are told apart by their colour:
//   warm  yellow-orange and fairly green, as torches, lanterns, campfires, candles and glowstone are: filtered
//         warmer, towards candlelight. Deep oranges (lava) and reds (redstone) are already warm and left alone
//   soul  cyan, blue well over red: soul torches, soul lanterns, soul fire, filtered colder and deeper
// Anything else (sea lanterns, end rods, froglights...) keeps Radiante PT's colour. Only the hue changes, at the
// light's own luminance, so rooms stay as bright. Candle Light sets how far; the flame itself (the glow a ray sees)
// takes three fifths of it, so the flame and the room it lights agree.
#include "aether/aether_common.glsl"

void hookBlockLight(inout vec3 radiance, uint kind) {
    float warmth = clamp(float(AETHER_TORCH_WARMTH), 0.0, 1.0);
    if (warmth <= 0.0) { return; }
    float luminance = aetherLuma(radiance);
    if (luminance <= 1e-6) { return; }
    vec3 hue = radiance / luminance;
    float warm = smoothstep(0.0, 0.35, hue.r - hue.b) * smoothstep(0.55, 0.8, hue.g) *
                 (1.0 - smoothstep(0.9, 1.4, hue.r - hue.b));
    float soul = smoothstep(0.3, 0.7, hue.b - hue.r) * smoothstep(0.8, 1.0, hue.g);
    vec3 tone = mix(vec3(1.0), vec3(1.0, 0.86, 0.66), warm);
    tone = mix(tone, vec3(0.75, 0.98, 1.15), soul);
    float amount = warmth * (kind == RADIANTE_BLOCK_LIGHT_GLOW ? 0.6 : 1.0);
    radiance = mix(radiance, aetherAtLuma(radiance * tone, luminance), amount);
}

#endif
