#ifndef RADIANTE_HOOK_SKY_EMISSION_GLSL
#define RADIANTE_HOOK_SKY_EMISSION_GLSL

// Mellow port: what glows in the night and rainy sky, the aurora and the rainbow. Every ray that reaches the sky comes
// through here, so the aurora also lights the world: snow, water and terrain under it take its green, faintly, and it
// shows in rough reflections. The rainbow is drawn only where the camera looks; the light it would cast is nothing
// next to the sun's.
//
// Aurora ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): get_aurora in shaders/global/atmosphere/sky.glsl,
// added before get_clouds in shaders/program/deferred1.fsh. As in Mellow it shows only where snow falls
// (AuroraStrength *= precipitationSmooth - 1 in get_aurora), fading in over a couple of seconds as the camera enters a
// snowy biome, unless Aurora Everywhere is on (Mellow's AURORA_EVERYWHERE). Its units are Mellow's, scaled to
// Radiante's night sky as the sky is (mellowSkyScale), and it drifts on Radiante's wind clock.
//
// Rainbow after Mellow's get_rainbow (same file) and its rainbowStrength (shaders.properties): Mellow shows a rainbow
// by day once the rain eases off. Mellow's arc is a hue wheel on a plane at a fixed place in the sky; this one is the
// port's own, a physically placed bow (see mellowRainbow).
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
// How much light the aurora casts on the world, against how bright it looks: 1 is as bright as it is seen, 0 none.
#ifndef MELLOW_AURORA_LIGHT
#    define MELLOW_AURORA_LIGHT 1.5
#endif
#ifndef MELLOW_RAINBOW
#    define MELLOW_RAINBOW 0.5
#endif

#include "mellow/mellow_clouds.glsl"

// How much aurora the biome allows: Mellow's precipitationSmooth - 1, 1 where it snows, 0 where it rains, below 0
// where nothing falls; 1 everywhere with Aurora Everywhere.
float mellowAuroraBiome() {
#if MELLOW_AURORA_EVERYWHERE == 0
    return radianteBiomePrecipitation() - 1.0;
#else
    return 1.0;
#endif
}

// get_aurora: two samples of the noise on a band 10 to 13 units up, green at the bottom and teal at the top. Cheap and
// smooth enough as it is to light the world too.
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
const vec3 MELLOW_AURORA_GREEN = vec3(0.10, 1.00, 0.32);
const vec3 MELLOW_AURORA_RED = vec3(1.00, 0.10, 0.18);
const float MELLOW_AURORA_BOTTOM = 10.0;
const float MELLOW_AURORA_TOP = 18.0;

// Fades towards the horizon, where the shell is crossed over thousands of units, and a little under the moon.
float mellowAuroraFade(vec3 dir, vec3 sunDir) {
    return smoothstep(0.02, 0.18, dir.y) * (1.0 - 0.35 * clamp(-sunDir.y, 0.0, 1.0));
}

// The port's own aurora, not Mellow's: curtains of light raymarched through a shell of the sky. Real aurorae glow
// between about 100 and 300 km up; the shell here is 10 to 18 sky units, Mellow's aurora plane being 10 units up.
// The curtains are thin sheets along the contour lines of slowly drifting, domain-warped noise (Mellow's own noise
// image), so they fold and ripple as real ones do; each has a sharp lower edge whose height wanders, a long soft
// fade upwards and vertical rays (noise that depends only on where the column stands, not on height). The colours
// are oxygen's: green 557.7 nm in the bright lower part, red 630 nm higher up and fainter, a magenta mix at the top,
// and a faint blue-violet fringe under the lower edge (nitrogen). 20 steps, the sky the camera sees only.
vec3 mellowAuroraRealistic(MellowTime t, vec3 dir, vec3 sunDir) {
    float strength = float(MELLOW_AURORA) * t.night * mellowAuroraBiome();
    if (strength <= 0.01 || dir.y <= 0.02) return vec3(0.0);
    float fade = mellowAuroraFade(dir, sunDir);

    const int STEPS = 20;
    const vec3 MAGENTA = vec3(0.75, 0.15, 0.65);
    const vec3 VIOLET = vec3(0.35, 0.20, 1.00);
    float near = MELLOW_AURORA_BOTTOM / dir.y;
    float far = MELLOW_AURORA_TOP / dir.y;
    float stepLength = (far - near) / float(STEPS);
    // Dithered start, against bands between the steps.
    float jitter = mellowRandom(dir.xz * 4096.0 + fract(mellowSeconds()));
    // Wrapped so the noise coordinates keep their precision over a long session.
    float time = mod(mellowSeconds(), 20000.0);

    vec3 light = vec3(0.0);
    for (int i = 0; i < STEPS; i++) {
        vec3 p = dir * (near + (float(i) + jitter) * stepLength);
        float h = (p.y - MELLOW_AURORA_BOTTOM) / (MELLOW_AURORA_TOP - MELLOW_AURORA_BOTTOM);
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

        vec3 color = mix(MELLOW_AURORA_GREEN, MELLOW_AURORA_RED * 0.55, smoothstep(0.12, 0.55, above));
        color = mix(color, MAGENTA * 0.45, smoothstep(0.5, 0.85, above));
        color = above < 0.0 ? VIOLET : color;
        light += color * (curtain * profile * rays * shimmer);
    }
    // Calibrated so the default strength (0.2) reads clearly against Radiante's night sky without going neon.
    return light * (6.0 / float(STEPS)) * strength * fade;
}

// The same aurora as the light it casts, for the rays that bounce off the world. They reach the sky in every
// direction, many per pixel, so the curtains, their rays and the per-frame dither would only be noise the denoiser
// and SHaRC have to smear out; the light on the ground is the curtains' average anyway. One sample of the
// domain warp's slow noise in the middle of the shell says how much curtain hangs over that part of the sky; the
// average of the raymarch above (curtain about 1/13, profile 0.29, rays 0.57, shimmer 0.85, times 6) is 0.065, and
// amount averages one half, hence 0.13. Mostly green, with some of the red above.
vec3 mellowAuroraRealisticGlow(MellowTime t, vec3 dir, vec3 sunDir) {
    float strength = float(MELLOW_AURORA) * t.night * mellowAuroraBiome();
    if (strength <= 0.01 || dir.y <= 0.02) return vec3(0.0);
    float time = mod(mellowSeconds(), 20000.0);
    vec2 xz = dir.xz * (0.5 * (MELLOW_AURORA_BOTTOM + MELLOW_AURORA_TOP) / dir.y) * 0.012;
    float amount = smoothstep(0.25, 0.75, textureLod(mellowNoiseTex, xz * 0.35 + vec2(time * 0.0006, 0.0), 0.0).r);
    vec3 color = mix(MELLOW_AURORA_GREEN, MELLOW_AURORA_RED * 0.55, 0.3);
    return color * (0.13 * amount) * strength * mellowAuroraFade(dir, sunDir);
}
#endif

// The rainbow, the port's own. Sunlight refracted, reflected once inside raindrops and refracted out leaves them
// most strongly at 42 degrees from the antisolar point (the point straight opposite the sun), and twice reflected at
// about 51 degrees, fainter and with its colours the other way round; between the two the sky is darker (Alexander's
// band) and inside the primary bow lighter. Each wavelength bends by its own angle (water's dispersion), so the bow
// is the sum of one narrow peak per wavelength: an Airy-like profile with a sharp edge on the outside of the primary
// (the inside of the secondary) and a tail towards the antisolar point, widened by the sun's half-degree disc.
//
// Where: around the antisolar point, so only the part above the horizon shows. With the sun higher than 42 degrees
// the whole primary is under the horizon, as in reality; low suns give the tall arcs. When: by day, while it rains
// lightly, which in Minecraft is as the rain starts and, mostly, as it stops (the rain gradient eases off over a few
// seconds; there is no clock of time since rain to keep it longer, as Mellow's wetness does). Not in thunder. No
// moonbow.
const int MELLOW_RAINBOW_BANDS = 7;
// 400 to 700 nm in steps of 50: the primary's and the secondary's angle from the antisolar point (degrees), and the
// wavelength's colour in linear sRGB, roughly the spectral locus clipped into the gamut.
const float MELLOW_RAINBOW_PRIMARY[MELLOW_RAINBOW_BANDS] = float[](40.6, 41.1, 41.6, 41.9, 42.1, 42.3, 42.4);
const float MELLOW_RAINBOW_SECONDARY[MELLOW_RAINBOW_BANDS] = float[](53.6, 52.8, 52.0, 51.4, 50.9, 50.5, 50.3);
const vec3 MELLOW_RAINBOW_COLOUR[MELLOW_RAINBOW_BANDS] =
    vec3[](vec3(0.20, 0.00, 0.60), vec3(0.00, 0.05, 1.00), vec3(0.00, 0.60, 0.45), vec3(0.25, 1.00, 0.00),
           vec3(1.00, 0.45, 0.00), vec3(0.90, 0.05, 0.00), vec3(0.30, 0.00, 0.00));
// The colours above summed, so the bands add up to white where they overlap.
const vec3 MELLOW_RAINBOW_WHITE = vec3(2.65, 2.15, 2.05);

// One wavelength's bow, x degrees inside its angle (outside, for the secondary): a sharp edge, a peak and a tail.
float mellowRainbowPeak(float x) {
    if (x < 0.0) return exp(-(x * x) / (0.3 * 0.3));
    return 0.7 * exp(-x / 0.7) + 0.3 * exp(-x / 6.0);
}

vec3 mellowRainbow(vec3 dir, vec3 sunDir) {
    if (float(MELLOW_RAINBOW) <= 0.0 || dir.y <= 0.0) return vec3(0.0);
    float rain = radianteRain();
    float weather = smoothstep(0.02, 0.2, rain) * (1.0 - smoothstep(0.4, 0.8, rain)) * (1.0 - radianteThunder());
    float sunUp = smoothstep(0.0, 0.06, sunDir.y);
    if (weather * sunUp <= 0.0) return vec3(0.0);
    float angle = degrees(acos(clamp(dot(dir, -sunDir), -1.0, 1.0)));
    if (angle < 25.0 || angle > 58.0) return vec3(0.0);
    vec3 bow = vec3(0.0);
    for (int i = 0; i < MELLOW_RAINBOW_BANDS; i++) {
        float primary = mellowRainbowPeak(MELLOW_RAINBOW_PRIMARY[i] - angle);
        float secondary = mellowRainbowPeak(angle - MELLOW_RAINBOW_SECONDARY[i]);
        bow += MELLOW_RAINBOW_COLOUR[i] * (primary + 0.4 * secondary);
    }
    bow /= MELLOW_RAINBOW_WHITE;
    // Lit by the sun, seen against the rain: as bright as a good part of the greyed sky there, fading into the
    // ground's haze at the horizon and at the ends of its angular range.
    float grey = 0.1 * smoothstep(-0.3, 0.3, sunDir.y);
    float sky = mix(mellowLuminance(textureLod(skyFull, dir, 0.0).rgb), grey, rain);
    float edges = smoothstep(25.0, 32.0, angle) * (1.0 - smoothstep(56.0, 58.0, angle));
    return bow * (0.6 * sky * float(MELLOW_RAINBOW) * weather * sunUp * edges * smoothstep(0.0, 0.03, dir.y));
}

vec3 hookSkyEmission(vec3 direction, bool seen) {
    vec3 sunDir = normalize(skyUBO.sunDirection);
    vec3 light = vec3(0.0);
    if (float(MELLOW_AURORA) > 0.0 && (seen || float(MELLOW_AURORA_LIGHT) > 0.0) && direction.y > 0.0) {
        MellowTime t = mellowTime(sunDir);
        if (t.night > 0.0) {
            vec3 aurora;
#if MELLOW_AURORA_STYLE == 1
            aurora = seen ? mellowAuroraRealistic(t, direction, sunDir)
                          : mellowAuroraRealisticGlow(t, direction, sunDir) * float(MELLOW_AURORA_LIGHT);
#else
            aurora = mellowAurora(t, direction) * (seen ? 1.0 : float(MELLOW_AURORA_LIGHT));
#endif
            if (any(greaterThan(aurora, vec3(0.0)))) light += aurora * mellowSkyScale(t, sunDir, true);
        }
    }
    if (seen) light += mellowRainbow(direction, sunDir);
    return light;
}

#endif
