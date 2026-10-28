#ifndef RADIANTE_HOOK_STARS_GLSL
#define RADIANTE_HOOK_STARS_GLSL

// Mellow port: Mellow's night sky, its stars and its aurora, under its clouds. Ported from Mellow by TheCMK (MIT,
// LICENSE-MELLOW): get_stars and get_aurora in shaders/global/atmosphere/sky.glsl, added before get_clouds in
// shaders/program/deferred1.fsh, and random in shaders/lib/util.glsl. Mellow draws no vanilla stars
// (gbuffers_skybasic.fsh), so with Mellow's stars on, Radiante's are hidden.
//
// What changed for Radiante: both are in Mellow's units, scaled to Radiante's night sky as the sky is
// (mellowSkyScale); they drift on Radiante's wind clock instead of frameTimeCounter. Mellow shows its aurora only in
// snowy biomes (precipitationSmooth), which the shaders here cannot tell, so it is off by default and, when on, shows
// everywhere (Mellow's AURORA_EVERYWHERE).
#ifndef MELLOW_STARS
#    define MELLOW_STARS 0.5
#endif
#ifndef MELLOW_AURORA
#    define MELLOW_AURORA 0.0
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

// get_aurora: two samples of the noise on a band 10 to 13 units up, green at the bottom and teal at the top.
vec3 mellowAurora(MellowTime t, vec3 dir) {
    float strength = float(MELLOW_AURORA) * t.night;
    if (strength <= 0.01 || dir.y <= 0.0) return vec3(0.0);
    const vec3 COLOR_TOP = pow(vec3(28.0, 255.0, 218.0) / 255.0, vec3(2.2));
    const vec3 COLOR_BOTTOM = pow(vec3(122.0, 255.0, 28.0) / 255.0, vec3(2.2));
    const float PLANE_BOTTOM = 10.0;
    const float PLANE_TOP = 10.0 + 3.0;
    const int SAMPLE_COUNT = 2;
    const float DITHER = 0.5;
    vec3 startPos = PLANE_BOTTOM / dir.y * dir;
    vec3 endPos = PLANE_TOP / dir.y * dir;
    vec3 stepVec = (endPos - startPos) / float(SAMPLE_COUNT);
    vec3 pos = stepVec * DITHER + startPos;
    // The noise repeats every 100 by 200 units of pos.xz at this scale; the wind is wrapped on that.
    vec2 wind = mod(mellowSeconds() * vec2(0.25, 0.33), vec2(100.0, 200.0));
    vec3 aurora = vec3(0.0);
    for (int i = 1; i <= SAMPLE_COUNT; i++) {
        float noise = textureLod(mellowNoiseTex, (pos.xz - wind) / vec2(100.0, 200.0), 0.0).r;
        noise = noise * noise;
        noise = noise * noise * noise * noise;
        noise *= smoothstep(0.0, 0.2, dir.y);
        aurora += noise * mix(COLOR_BOTTOM, COLOR_TOP, smoothstep(0.0, 1.0, (float(i) + DITHER - 1.0) / float(SAMPLE_COUNT)));
        pos += stepVec;
    }
    return aurora * strength / float(SAMPLE_COUNT);
}

float hookStars(inout vec3 radiance, vec3 direction) {
    if (float(MELLOW_STARS) <= 0.0 && float(MELLOW_AURORA) <= 0.0) return 1.0;
    vec3 sunDir = normalize(skyUBO.sunDirection);
    MellowTime t = mellowTime(sunDir);
    if (t.night > 0.0 && direction.y > 0.0) {
        float scale = mellowSkyScale(t, sunDir, true);
        radiance += (vec3(mellowStars(t, direction)) + mellowAurora(t, direction)) * scale;
    }
    return float(MELLOW_STARS) > 0.0 ? 0.0 : 1.0;
}

#endif
