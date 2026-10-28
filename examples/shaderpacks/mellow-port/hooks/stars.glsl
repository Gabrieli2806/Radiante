#ifndef RADIANTE_HOOK_STARS_GLSL
#define RADIANTE_HOOK_STARS_GLSL

// Mellow port: Mellow's stars, under its clouds. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): get_stars in
// shaders/global/atmosphere/sky.glsl, added before get_clouds in shaders/program/deferred1.fsh, and random in
// shaders/lib/util.glsl. Mellow draws no vanilla stars (gbuffers_skybasic.fsh), so with Mellow's stars on, Radiante's
// are hidden. Mellow's aurora is in hooks/sky_emission.glsl, which every ray reaching the sky sees, so it also lights
// the world.
//
// What changed for Radiante: the stars are in Mellow's units, scaled to Radiante's night sky as the sky is
// (mellowSkyScale), and drift on Radiante's wind clock instead of frameTimeCounter.
#ifndef MELLOW_STARS
#    define MELLOW_STARS 0.5
#endif

#include "mellow/mellow_clouds.glsl"

// get_stars: one star per cell of a grid on the sky dome (STAR_SIZE 1), fading out towards the horizon, only at night.
float mellowStars(MellowTime t, vec3 dir) {
    vec3 starCoord = dir / (dir.y + length(dir.xz));
    starCoord.x += mellowSeconds() * 0.001;
    const float STAR_CELLS = 512.0;
    starCoord = floor(starCoord * STAR_CELLS) / STAR_CELLS;
    float visibility = smoothstep(0.0, 0.1, starCoord.y) * t.night;
    return max(0.0, mellowRandom(starCoord.xz) - 0.996) * 50.0 * visibility * float(MELLOW_STARS);
}

float hookStars(inout vec3 radiance, vec3 direction) {
    if (float(MELLOW_STARS) <= 0.0) return 1.0;
    vec3 sunDir = normalize(skyUBO.sunDirection);
    MellowTime t = mellowTime(sunDir);
    if (t.night > 0.0 && direction.y > 0.0) {
        radiance += vec3(mellowStars(t, direction)) * mellowSkyScale(t, sunDir, true);
    }
    return 0.0;
}

#endif
