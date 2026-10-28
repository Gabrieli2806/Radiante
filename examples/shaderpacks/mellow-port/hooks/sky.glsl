#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Mellow port: Mellow's sky, drawn into the sky every ray that leaves the world sees, so it shows in the sky, in
// reflections and in the light the sky casts. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): get_sky in
// shaders/global/atmosphere/sky.glsl. The sky's hue and gradient are Mellow's, its brightness Radiante's
// (mellowSkyScale). Mellow's clouds are in hooks/clouds.glsl, drawn after the rain and over the sun and moon, and its
// stars in hooks/stars.glsl.
#ifndef MELLOW_SKY
#    define MELLOW_SKY 1.0
#endif

#include "mellow/mellow_common.glsl"

void hookSky(inout vec3 radiance, vec3 direction) {
    vec3 sunDir = normalize(skyUBO.sunDirection);
    MellowTime t = mellowTime(sunDir);
    vec3 sky = mellowSky(t, direction, sunDir) * mellowSkyScale(t, sunDir, false);
    radiance = mix(radiance, sky, float(MELLOW_SKY));
}

#endif
