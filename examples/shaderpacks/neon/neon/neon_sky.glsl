#ifndef NEON_SKY_GLSL
#define NEON_SKY_GLSL

// Neon Dusk: the sky's look, for the overworld miss shader only (hooks/sky.glsl, stars.glsl and celestial.glsl): it
// reads the payload (mainRay) and the sky cube (skyFull) that shader declares before it includes the hooks.
//
// Two skies. What the camera sees (its own ray, through glass or water, and the reflections it sees in smooth
// surfaces) is the neon picture: a gradient, the striped retro sun, a grid on the horizon. What lights the world (rays
// that bounced off it) is Radiante's own sky, at its own brightness in every direction, only partly turned to the
// look's hue (NEON_SKY_LIGHT). Those rays decide how bright and what colour the shade is; drawn the neon picture, the
// world flooded with magenta, and the sun's stripes and the grid's lines would have been noise the denoiser cannot
// clean.

#include "neon/neon_common.glsl"

#ifndef NEON_SKY_LIGHT
#    define NEON_SKY_LIGHT 0.3
#endif
#ifndef NEON_GRID
#    define NEON_GRID 0.5
#endif

// The sky the camera sees, as opposed to light gathered for a surface: the camera's ray (bounce 0), the camera's own
// path through glass and water, and a view chain, the reflection a smooth surface shows the camera
// (RAY_VIEW_BOUNCE_MARK, pt_bounce.glsl). The wet ground's reflections are of the neon sky that way.
bool neonSkySeen() {
    return rayBounce(mainRay) == 0u || rayCameraPath(mainRay) || mainRay.pad0 == RAY_VIEW_BOUNCE_MARK;
}

// How bright Radiante's own sky is: the zenith and the horizon a quarter turn from the sun (out of its glow),
// averaged. Everything the pack draws in the sky is scaled to it, so the exposure stays Radiante's through the day
// and the night.
float neonSkyReference() {
    vec3 sun = radianteSunDirection();
    vec3 side = cross(vec3(0.0, 1.0, 0.0), sun);
    side = dot(side.xz, side.xz) > 1e-6 ? normalize(vec3(side.x, 0.05, side.z)) : vec3(0.9987, 0.05, 0.0);
    return 0.5 * (neonLuminance(textureLod(skyFull, vec3(0.0, 1.0, 0.0), 0.0).rgb) +
                  neonLuminance(textureLod(skyFull, side, 0.0).rgb));
}

// A gradient by height: below the horizon, at it, just over it, half way up, overhead.
vec3 neonGradient(vec3 below, vec3 horizon, vec3 low, vec3 mid, vec3 zenith, float y) {
    if (y < 0.0) { return mix(horizon, below, smoothstep(0.0, 0.12, -y)); }
    vec3 c = mix(horizon, low, smoothstep(0.0, 0.07, y));
    c = mix(c, mid, smoothstep(0.05, 0.35, y));
    return mix(c, zenith, smoothstep(0.3, 0.9, y));
}

// The three looks' colours. Their brightness from the horizon up is part of the look (a dusk is bright low down and
// dark overhead); neonLookNorm takes the overall level out, so only that shape is kept.
vec3 neonLookColour(vec3 dir, vec3 w) {
    vec3 c = vec3(0.0);
    if (w.x > 0.0) {
        // Daylight Neon: a teal sky over a pink and lavender haze.
        c += w.x * neonGradient(vec3(0.42, 0.36, 0.55), vec3(1.0, 0.72, 0.82), vec3(0.95, 0.76, 0.95),
                                vec3(0.45, 0.75, 1.0), vec3(0.18, 0.52, 0.95), dir.y);
    }
    if (w.y > 0.0) {
        // Dusk: orange on the horizon, hot pink over it, magenta and purple up to an indigo zenith.
        c += w.y * neonGradient(vec3(0.12, 0.03, 0.18), vec3(1.0, 0.45, 0.16), vec3(1.0, 0.22, 0.45),
                                vec3(0.6, 0.12, 0.55), vec3(0.12, 0.05, 0.32), dir.y);
    }
    if (w.z > 0.0) {
        // Night: deep purple, a magenta glow along the horizon like a city's far off.
        c += w.z * neonGradient(vec3(0.03, 0.01, 0.06), vec3(0.5, 0.12, 0.45), vec3(0.32, 0.08, 0.38),
                                vec3(0.1, 0.04, 0.22), vec3(0.03, 0.02, 0.10), dir.y);
    }
    return c;
}

// The look's own level, measured where neonSkyReference measures Radiante's sky.
float neonLookNorm(vec3 w) {
    vec3 side = vec3(0.9987, 0.05, 0.0);
    return max(0.5 * (neonLuminance(neonLookColour(vec3(0.0, 1.0, 0.0), w)) +
                      neonLuminance(neonLookColour(side, w))), 1e-5);
}

// How bright each look is against Radiante's sky. Dusk dimmer: at the noon sky's full brightness the tone curve
// paled it into pastel pink, at 0.6 it keeps its colour. Night dark against a sunlit world, so the Midnight look by
// day reads as night overhead; at the real night Radiante's sky is dark already and kept as it is.
float neonLookLevel(vec3 w) {
    return w.x + 0.6 * w.y + w.z * mix(0.22, 1.0, radianteNight());
}

// The glow of the sky towards the retro sun, along the horizon under it, while the sun is low: by azimuth, fading
// upwards and into the ground's haze below the horizon. Gone with the real sun high or set.
vec3 neonSunSideGlow(vec3 dir, vec3 w, vec3 sunCentre) {
    vec2 flatDir = dir.xz;
    vec2 flatSun = sunCentre.xz;
    if (dot(flatDir, flatDir) < 1e-8 || dot(flatSun, flatSun) < 1e-8) { return vec3(0.0); }
    float towards = max(dot(normalize(flatDir), normalize(flatSun)), 0.0);
    float low = (1.0 - smoothstep(0.2, 0.5, sunCentre.y)) * smoothstep(-0.25, -0.02, sunCentre.y);
    float glow = towards * towards * towards * exp(-max(dir.y, 0.0) / 0.22) * smoothstep(-0.12, 0.0, dir.y) * low;
    vec3 colour = w.x * vec3(1.0, 0.82, 0.86) + w.y * vec3(1.0, 0.42, 0.25) + w.z * vec3(0.8, 0.18, 0.6);
    return colour * glow * 0.9;
}

// The retro sun: a big disc, yellow at the top through orange to magenta low down, cut from a little over its middle
// down by horizontal gaps that widen towards the bottom, sinking behind the horizon. At 2.5 times the sky's
// brightness: bright enough to bloom, not so bright that the tone curve turns the whole gradient white (4 times was
// cream in an offline preview). Seen only. Its edges are smoothed over about a pixel at 1080p and a 70 degree view
// (0.0012 radians), so the disc and its gaps do not stair-step.
vec3 neonRetroSun(vec3 dir, vec3 centre, float level) {
    float z = dot(dir, centre);
    if (z <= 0.0) { return vec3(0.0); }
    vec3 right = cross(centre, vec3(0.0, 1.0, 0.0));
    if (dot(right, right) < 1e-4) {
        // Straight overhead (the real sun at noon): any frame will do, the path's axis gives a steady one.
        vec3 axis = skyUBO.celestialAxis.xyz;
        right = axis - centre * dot(axis, centre);
        if (dot(right, right) < 1e-6) { right = vec3(1.0, 0.0, 0.0); }
    }
    right = normalize(right);
    vec3 up = cross(right, centre);
    float radius = tan(radians(clamp(float(NEON_SUN_SIZE), 2.0, 30.0)));
    vec2 q = vec2(dot(dir, right), dot(dir, up)) / (z * radius);
    float distance = length(q);
    float aa = 0.0012 / radius;

    // The halo round it, pink, fading over about a radius.
    vec3 light = vec3(1.0, 0.3, 0.55) * (0.45 * exp(-max(distance - 1.0, 0.0) * 3.5)) * step(1.0, distance);
    float cover = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, distance);
    if (cover > 0.0) {
        float v = q.y;
        vec3 top = vec3(1.0, 0.85, 0.2);
        vec3 middle = vec3(1.0, 0.4, 0.08);
        vec3 bottom = vec3(1.0, 0.06, 0.5);
        vec3 colour = v > 0.25 ? mix(middle, top, smoothstep(0.25, 1.0, v)) : mix(bottom, middle, smoothstep(-0.6, 0.25, v));
        // The gaps: seven bands from v 0.4 down to the bottom, each gap a larger share of its band, the first a thin
        // line.
        float s = (0.4 - v) / 1.4;
        if (s > 0.0) {
            const float BANDS = 7.0;
            float fromGap = abs(fract(s * BANDS + 0.5) - 0.5);
            float halfGap = 0.05 + 0.3 * s;
            float bandAa = aa * BANDS / 1.4 + 0.01;
            cover *= smoothstep(halfGap - bandAa, halfGap + bandAa, fromGap);
        }
        light = mix(light, colour * 2.5, cover);
    }
    // Into the horizon as radiante-pt's own discs go (CELESTIAL_HORIZON_Y), so the grid below stands in front of it.
    float horizon = smoothstep(-0.021, -0.009, dir.y);
    return light * level * horizon;
}

// The grid of a synthwave horizon: lines on a plane under the sky, in perspective, drifting towards the camera, and
// a glowing line along the horizon. The sky below the horizon only shows past the world's edge, over the sea far out
// and from high up, so it is a faint backdrop. Seen only. The lines keep about a pixel and a half wide whatever their
// distance and fade before the horizon, where they would get finer than a pixel and shimmer.
vec3 neonGrid(vec3 dir, vec3 sunCentre, float level) {
    vec2 forward = dot(sunCentre.xz, sunCentre.xz) > 1e-6 ? normalize(sunCentre.xz) : vec2(-1.0, 0.0);
    float towards = max(dot(normalize(dir.xz + vec2(1e-6)), forward), 0.0);
    // The horizon line, pinker and brighter under the sun.
    vec3 light = mix(vec3(1.0, 0.25, 0.65), vec3(1.0, 0.45, 0.35), towards) *
                 (exp(-abs(dir.y) / 0.008) * (0.5 + 0.5 * towards) * 1.4);
    float down = -dir.y;
    if (down > 0.03) {
        // The plane one unit under the eye: lines across the view (towards the sun) and along it.
        vec2 p = dir.xz / down;
        vec2 across = vec2(-forward.y, forward.x);
        // Two cells per unit; a quarter of a cell per second, so over the game's day (1200 s, the period
        // worldUBO.gameTime wraps at) it moves a whole number of cells and never jumps.
        float drift = fract(worldUBO.gameTime * 1200.0 * 0.25);
        float a = dot(p, forward) * 2.0 + drift;
        float b = dot(p, across) * 2.0;
        float widthA = 0.0036 / (down * down);
        float widthB = 0.0036 / down;
        float fromA = abs(fract(a + 0.5) - 0.5);
        float fromB = abs(fract(b + 0.5) - 0.5);
        float lineA = 1.0 - smoothstep(0.5 * widthA, 1.5 * widthA, fromA);
        float lineB = 1.0 - smoothstep(0.5 * widthB, 1.5 * widthB, fromB);
        // A soft glow either side of each line.
        float glow = exp(-fromA / (4.0 * widthA)) + exp(-fromB / (4.0 * widthB));
        float lines = max(lineA, lineB) + 0.15 * glow;
        float fade = smoothstep(0.03, 0.16, down) * (1.0 - smoothstep(0.6, 0.95, down));
        light += vec3(1.0, 0.15, 0.75) * lines * fade * 0.9;
    }
    return light * level;
}

#endif
