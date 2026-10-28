#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Mellow port: Mellow's sky, drawn into the sky every ray that leaves the world sees, so it shows in the sky, in
// reflections and in the light the sky casts. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): get_sky in
// shaders/global/atmosphere/sky.glsl. The sky's hue and gradient are Mellow's, its brightness Radiante's
// (mellowSkyScale). Mellow's clouds are in hooks/clouds.glsl, drawn after the rain and over the sun and moon, and its
// stars in hooks/stars.glsl.
//
// Two departures, from side-by-side shots against Radiante PT and Mellow's own pictures:
// - Mellow lights the world with its own flat ambient (SKY_GROUND, a pale grey-blue), not with the sky's colour. Here
//   the sky is the light, and Mellow's deep zenith blue turned the whole world blue and stone purple at dusk. The rays
//   that bounce off the world (what the sky casts) take only MELLOW_SKY_LIGHT of Mellow's hue, the rest Radiante's.
// - Mellow's colours are graded by its ACES curve, which pales a saturated blue; Radiante's tone curve keeps it, and
//   the noon sky came out a deep electric blue. The sky the camera sees is pulled towards grey by MELLOW_SKY_TONE.
#ifndef MELLOW_SKY
#    define MELLOW_SKY 1.0
#endif

const float MELLOW_SKY_LIGHT = 0.35;
const float MELLOW_SKY_TONE = 0.78;

#include "mellow/mellow_common.glsl"

void hookSky(inout vec3 radiance, vec3 direction) {
    vec3 sunDir = normalize(skyUBO.sunDirection);
    MellowTime t = mellowTime(sunDir);
    vec3 sky = mellowSky(t, direction, sunDir) * mellowSkyScale(t, sunDir, false);
    bool seen = rayBounce(mainRay) == 0u || rayCameraPath(mainRay);
    if (seen) {
        sky = mellowSaturation(sky, MELLOW_SKY_TONE);
        // Mellow's dusk is gold from the horizon up (its glare through its fog, warmed by ACES); with Radiante's curve
        // the blue overhead stayed and mixed with the glare into lilac. The camera's sky is warmed towards the glare's
        // amber in proportion to the twilight, at its own brightness.
        float dusk = t.sunset + t.sunrise;
        vec3 amber = mellowToLinear(vec3(1.0, 0.7, 0.45)) * (mellowLuminance(sky) / 0.55);
        sky = mix(sky, amber, 0.45 * dusk * dusk);
    } else {
        // Mellow's hue, at Radiante's brightness for that direction, only partly.
        float own = mellowLuminance(radiance);
        vec3 hued = sky * (own / max(mellowLuminance(sky), 1e-6));
        sky = mix(radiance, hued, MELLOW_SKY_LIGHT);
    }
    radiance = mix(radiance, sky, float(MELLOW_SKY));
}

#endif
