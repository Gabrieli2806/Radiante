#ifndef RADIANTE_HOOK_CELESTIAL_GLSL
#define RADIANTE_HOOK_CELESTIAL_GLSL

// Neon Dusk: the real sun's disc is hidden where the camera looks, since hooks/sky.glsl draws the retro sun there
// instead (at the real sun with the real cycle, low over the sunset when locked). Rays that bounced off the world
// still meet radiante-pt's own disc, so the light the sky gives stays Radiante's. With the real cycle and the retro
// sun at 0, radiante-pt's disc is left as it is. The moon is radiante-pt's.
//
// The sunlight itself is sampled apart from the disc and does not change: locked, the world is still lit from where
// the real sun is (see neonSunCentre).
#include "neon/neon_sky.glsl"

void hookSunDisc(inout vec3 radiance, vec3 direction, vec3 sunDirection, vec3 sunLight) {
    if (!neonSkySeen()) { return; }
    if (int(NEON_SKY_MODE) == 0 && float(NEON_SUN) <= 0.0) { return; }
    radiance *= 1.0 - clamp(float(NEON_SKY_STRENGTH), 0.0, 1.0);
}

void hookMoonDisc(inout vec3 radiance, vec3 direction, vec3 moonDirection, vec3 moonLight) {}

#endif
