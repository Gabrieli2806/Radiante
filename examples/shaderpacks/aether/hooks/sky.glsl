#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Aether: a painted sky. A soft gradient from a deep (not electric) blue overhead to a luminous pastel horizon by
// day; at golden hour a periwinkle vault over an amber horizon on the sun's side (rose-gold in the morning) and
// lavender opposite; at blue hour an indigo vault, a rose glow where the sun went down and the Belt of Venus, a pink
// band over the earth's blue shadow, opposite it; navy at night. Rain greys it before radiante-pt greys the whole
// sky (after this hook).
//
// Only the hue and the gradient are the pack's: each direction keeps Radiante PT's own brightness there (a little
// brighter at the horizon and deeper overhead for the camera), so the exposure stays Radiante's. The sky is also the
// light the world is lit by, since bounce rays reach it: a strongly coloured sky tints everything. So the rays that
// gather light get Radiante PT's sky with only Sky Light Tint of the pack's hue, at Radiante's brightness exactly;
// the camera, glass, water and windows see the painted sky (aetherSeen).
#include "aether/aether_sky.glsl"

void hookSky(inout vec3 radiance, vec3 direction) {
    float strength = clamp(float(AETHER_SKY), 0.0, 1.0);
    if (strength <= 0.0) { return; }
    AetherTime t = aetherTime();
    vec3 paint = aetherSkyPaint(direction, radianteSunDirection(), t);
    paint = aetherSaturate(paint, 1.0 - clamp(radianteRain() * 0.7 + radianteThunder() * 0.3, 0.0, 1.0));
    float own = aetherLuma(max(radiance, vec3(0.0)));
    if (aetherSeen()) {
        radiance = mix(radiance, aetherAtLuma(paint, own * aetherSkyLift(direction, t)), strength);
    } else {
        float light = clamp(float(AETHER_SKY_LIGHT), 0.0, 1.0);
        radiance = mix(radiance, aetherAtLuma(paint, own), strength * light);
    }
}

#endif
