#ifndef RADIANTE_HOOK_DIMENSION_SKY_GLSL
#define RADIANTE_HOOK_DIMENSION_SKY_GLSL

// Aether: a deep, quiet End sky and a warmer Nether haze.
//
// The End: an indigo vault turning violet towards the horizon, with a thin teal-green glow along the horizon itself;
// for the camera, slow veils of magenta and teal nebula drifting round the horizon and a field of faint stars over
// Radiante PT's own, turning slowly. Every direction keeps Radiante PT's brightness there (radiante-pt keeps the sky
// it shows much darker than the light it casts on the islands), and the End flash still flares through it. The light
// the sky casts on the islands takes only two fifths of the hue, so the end stone keeps its look. The End's fog takes
// the horizon's colour at its own brightness, so far islands melt into the sky. End Sky sets how much.
//
// The Nether: no sky to paint (radiante-pt has none there); Nether Haze thickens its fog (1 is Radiante PT's) and
// warms it a little as it thickens.
//
// Included by the miss shader (the skies) and by world/world.rgen (the fogs), so only the world and sky uniforms
// and the pack's noise image are used here.
#include "aether/aether_noise.glsl"
#include "aether/aether_stars.glsl"

vec3 aetherEndPaint(vec3 dir) {
    float horizon = exp(-abs(dir.y) * 3.0);
    vec3 paint = mix(vec3(0.10, 0.08, 0.28), vec3(0.42, 0.24, 0.62), horizon);
    return mix(paint, vec3(0.20, 0.55, 0.55), exp(-dir.y * dir.y / 0.004) * 0.35);
}

void hookNetherSky(inout vec3 radiance, vec3 direction) {}

void hookEndSky(inout vec3 radiance, vec3 direction, bool seen) {
    float strength = clamp(float(AETHER_END_SKY), 0.0, 1.0);
    if (strength <= 0.0) { return; }
    float own = aetherLuma(max(radiance, vec3(0.0)));
    vec3 styled = aetherAtLuma(aetherEndPaint(direction), own);
    if (!seen) {
        radiance = mix(radiance, styled, 0.4 * strength);
        return;
    }
    // Nebula veils round the horizon, turning once in about half an hour, and faint stars, both scaled to the
    // brightness of the End sky at the horizon.
    float time = mod(aetherSeconds(), 1800.0);
    vec3 turned = aetherRotate(direction, vec3(0.0, 1.0, 0.0), time * (2.0 * AETHER_PI / 1800.0));
    vec2 uv = turned.xz / (abs(turned.y) + 0.6) * 0.35;
    float veil = smoothstep(0.5, 0.9, aetherNoiseAt(uv + vec2(0.2, 0.7)).a) * (1.0 - smoothstep(0.3, 0.9, abs(direction.y)));
    vec3 tint = mix(vec3(0.85, 0.30, 0.80), vec3(0.25, 0.80, 0.75), aetherNoiseAt(uv * 2.0 + vec2(0.6, 0.1)).r);
    float horizonLight = aetherLuma(max(worldUBO.fogColor.rgb, vec3(0.0))) * 0.15;
    vec3 stars = aetherStarLayer(turned, 150.0, 0.08, 113u, time, 0.3);
    styled += (tint * (1.2 * veil) + stars * 4.0) * horizonLight;
    radiance = mix(radiance, styled, strength);
}

void hookNetherFog(inout vec3 colour, inout float density) {
    float haze = clamp(float(AETHER_NETHER_HAZE), 0.25, 3.0);
    density *= haze;
    vec3 warm = aetherAtLuma(colour * vec3(1.10, 0.90, 0.80), aetherLuma(colour));
    colour = mix(colour, warm, clamp((haze - 1.0) * 0.6, 0.0, 0.5));
}

void hookEndFog(inout vec3 colour, inout float density) {
    float strength = clamp(float(AETHER_END_SKY), 0.0, 1.0);
    vec3 horizon = aetherEndPaint(vec3(1.0, 0.0, 0.0));
    colour = mix(colour, aetherAtLuma(horizon, aetherLuma(colour)), strength);
}

#endif
