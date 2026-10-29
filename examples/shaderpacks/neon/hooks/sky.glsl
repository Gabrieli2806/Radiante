#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Neon Dusk: the synthwave sky. Before the rain greys the sky, so rain washes the whole picture out as it does
// radiante-pt's sky.
//
// The sky the camera sees (neonSkySeen, also in smooth reflections) is the look's gradient, scaled to Radiante's own
// sky brightness (neonSkyReference) and the look's level, with the retro sun, its glow along the horizon and the grid
// added, all in proportion to that brightness: a locked dusk at noon is as bright as the noon sky was, only in dusk
// colours, so the exposure and the bloom stay sane. With the real cycle the brightness follows the real sky's shape
// half way too (the bright side towards the real sun at dusk).
//
// The sky that lights the world (bounce rays) keeps Radiante's brightness in every direction and only takes
// NEON_SKY_LIGHT of the look's hue, desaturated: the shade turns violet at dusk without the world flooding with one
// colour. No sun, grid or other detail there.
#include "neon/neon_sky.glsl"

void hookSky(inout vec3 radiance, vec3 direction) {
    vec3 w = neonLookWeights();
    if (!neonSkySeen()) {
        float tint = clamp(float(NEON_SKY_LIGHT), 0.0, 1.0);
        if (tint <= 0.0) { return; }
        vec3 hue = neonLookColour(direction, w);
        hue = mix(vec3(neonLuminance(hue)), hue, 0.6);
        hue /= max(neonLuminance(hue), 1e-5);
        radiance = mix(radiance, hue * neonLuminance(radiance), tint);
        return;
    }

    float strength = clamp(float(NEON_SKY_STRENGTH), 0.0, 1.0);
    if (strength <= 0.0) { return; }
    float reference = neonSkyReference();
    vec3 sunCentre = neonSunCentre();
    vec3 sky = neonLookColour(direction, w) * (reference * neonLookLevel(w) / neonLookNorm(w));
    if (int(NEON_SKY_MODE) == 0) {
        // The real cycle: half way (in brightness ratio) towards the real sky's own shape.
        float ratio = neonLuminance(radiance) / max(neonLuminance(sky), 1e-6);
        sky *= sqrt(clamp(ratio, 0.25, 4.0));
    }
    sky += neonSunSideGlow(direction, w, sunCentre) * reference * neonLookLevel(w);

    float sun = max(float(NEON_SUN), 0.0);
    if (sun > 0.0) { sky += neonRetroSun(direction, sunCentre, reference * sun); }
    float grid = max(float(NEON_GRID), 0.0);
    if (grid > 0.0) { sky += neonGrid(direction, sunCentre, reference * grid); }

    radiance = mix(radiance, sky, strength);
}

#endif
