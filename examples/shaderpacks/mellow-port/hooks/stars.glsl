#ifndef RADIANTE_HOOK_STARS_GLSL
#define RADIANTE_HOOK_STARS_GLSL

// Mellow port: Mellow's night sky, its stars and its aurora, under its clouds. Ported from Mellow by TheCMK (MIT,
// LICENSE-MELLOW): get_stars and get_aurora in shaders/global/atmosphere/sky.glsl, added before get_clouds in
// shaders/program/deferred1.fsh, and random in shaders/lib/util.glsl. Mellow draws no vanilla stars
// (gbuffers_skybasic.fsh), so with Mellow's stars on, Radiante's are hidden.
//
// What changed for Radiante: both are in Mellow's units, scaled to Radiante's night sky as the sky is
// (mellowSkyScale); they drift on Radiante's wind clock instead of frameTimeCounter. As in Mellow the aurora shows only
// where snow falls (AuroraStrength *= precipitationSmooth - 1 in get_aurora), fading in over a couple of seconds as
// the camera enters a snowy biome, unless Aurora Everywhere is on (Mellow's AURORA_EVERYWHERE).
#ifndef MELLOW_STARS
#    define MELLOW_STARS 0.5
#endif
#ifndef MELLOW_AURORA
#    define MELLOW_AURORA 0.2
#endif
#ifndef MELLOW_AURORA_EVERYWHERE
#    define MELLOW_AURORA_EVERYWHERE 0
#endif
// 0 Mellow's own aurora, 1 the port's realistic one (mellowAuroraRealistic). Compiled in: only one is in the shader.
#ifndef MELLOW_AURORA_STYLE
#    define MELLOW_AURORA_STYLE 1
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

// How much aurora the biome allows: Mellow's precipitationSmooth - 1, 1 where it snows, 0 where it rains, below 0
// where nothing falls; 1 everywhere with Aurora Everywhere.
float mellowAuroraBiome() {
#if MELLOW_AURORA_EVERYWHERE == 0
    return radianteBiomePrecipitation() - 1.0;
#else
    return 1.0;
#endif
}

// get_aurora: two samples of the noise on a band 10 to 13 units up, green at the bottom and teal at the top.
vec3 mellowAurora(MellowTime t, vec3 dir) {
    float strength = float(MELLOW_AURORA) * t.night * mellowAuroraBiome();
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

#if MELLOW_AURORA_STYLE == 1
// The port's own aurora, not Mellow's: curtains of light raymarched through a shell of the sky. Real aurorae glow
// between about 100 and 300 km up; the shell here is 10 to 18 sky units, Mellow's aurora plane being 10 units up.
// The curtains are thin sheets along the contour lines of slowly drifting, domain-warped noise (Mellow's own noise
// image), so they fold and ripple as real ones do; each has a sharp lower edge whose height wanders, a long soft
// fade upwards and vertical rays (noise that depends only on where the column stands, not on height). The colours
// are oxygen's: green 557.7 nm in the bright lower part, red 630 nm higher up and fainter, a magenta mix at the top,
// and a faint blue-violet fringe under the lower edge (nitrogen). 20 steps, night sky pixels only.
vec3 mellowAuroraRealistic(MellowTime t, vec3 dir, vec3 sunDir) {
    float strength = float(MELLOW_AURORA) * t.night * mellowAuroraBiome();
    if (strength <= 0.01 || dir.y <= 0.02) return vec3(0.0);
    // Fades towards the horizon, where the shell is crossed over thousands of units, and a little under the moon.
    float fade = smoothstep(0.02, 0.18, dir.y) * (1.0 - 0.35 * clamp(-sunDir.y, 0.0, 1.0));

    const int STEPS = 20;
    const float BOTTOM = 10.0;
    const float TOP = 18.0;
    const vec3 GREEN = vec3(0.10, 1.00, 0.32);
    const vec3 RED = vec3(1.00, 0.10, 0.18);
    const vec3 MAGENTA = vec3(0.75, 0.15, 0.65);
    const vec3 VIOLET = vec3(0.35, 0.20, 1.00);
    float near = BOTTOM / dir.y;
    float far = TOP / dir.y;
    float stepLength = (far - near) / float(STEPS);
    // Dithered start, against bands between the steps.
    float jitter = mellowRandom(dir.xz * 4096.0 + fract(mellowSeconds()));
    // Wrapped so the noise coordinates keep their precision over a long session.
    float time = mod(mellowSeconds(), 20000.0);

    vec3 light = vec3(0.0);
    for (int i = 0; i < STEPS; i++) {
        vec3 p = dir * (near + (float(i) + jitter) * stepLength);
        float h = (p.y - BOTTOM) / (TOP - BOTTOM);
        vec2 xz = p.xz * 0.012;
        // Domain warp: the curtains' lines bend and slowly fold.
        vec2 warp = vec2(textureLod(mellowNoiseTex, xz * 0.35 + vec2(time * 0.0006, 0.0), 0.0).r,
                         textureLod(mellowNoiseTex, xz * 0.35 + vec2(0.37, time * 0.0005), 0.0).r) - 0.5;
        float n = textureLod(mellowNoiseTex, xz + warp * 1.3 + vec2(time * 0.0012, 0.0), 0.0).r;
        // Thin sheets along the noise's mid contour.
        float curtain = pow(1.0 - abs(2.0 * n - 1.0), 12.0);
        if (curtain < 0.01) continue;
        // The lower edge's height wanders along the curtain; below it, nothing but a faint fringe.
        float edge = 0.04 + 0.14 * textureLod(mellowNoiseTex, xz * 1.7 + vec2(0.71, 0.13), 0.0).r;
        float above = h - edge;
        float profile = above >= 0.0 ? exp(-above * 3.2) * smoothstep(0.0, 0.012, above)
                                      : 0.25 * exp(above * 60.0);
        // Rays: brightness that changes across the curtain but not up it, drifting sideways.
        float ray = textureLod(mellowNoiseTex, xz * 9.0 + vec2(time * 0.003, 0.0), 0.0).r;
        float rays = 0.35 + 0.65 * ray * ray;
        // Gentle shimmer: the curtain pulses slowly, a little out of step along its length.
        float shimmer = 0.85 + 0.15 * sin(time * 0.9 + n * 25.0);

        vec3 color = mix(GREEN, RED * 0.55, smoothstep(0.12, 0.55, above));
        color = mix(color, MAGENTA * 0.45, smoothstep(0.5, 0.85, above));
        color = above < 0.0 ? VIOLET : color;
        light += color * (curtain * profile * rays * shimmer);
    }
    // Calibrated so the default strength (0.2) reads clearly against Radiante's night sky without going neon.
    return light * (6.0 / float(STEPS)) * strength * fade;
}
#endif

float hookStars(inout vec3 radiance, vec3 direction) {
    if (float(MELLOW_STARS) <= 0.0 && float(MELLOW_AURORA) <= 0.0) return 1.0;
    vec3 sunDir = normalize(skyUBO.sunDirection);
    MellowTime t = mellowTime(sunDir);
    if (t.night > 0.0 && direction.y > 0.0) {
        float scale = mellowSkyScale(t, sunDir, true);
#if MELLOW_AURORA_STYLE == 1
        vec3 aurora = mellowAuroraRealistic(t, direction, sunDir);
#else
        vec3 aurora = mellowAurora(t, direction);
#endif
        radiance += (vec3(mellowStars(t, direction)) + aurora) * scale;
    }
    return float(MELLOW_STARS) > 0.0 ? 0.0 : 1.0;
}

#endif
