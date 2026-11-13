#ifndef AETHER_RAINBOW_GLSL
#define AETHER_RAINBOW_GLSL

#include "aether/aether_common.glsl"
#include "aether/aether_sky.glsl"
#include <radiante/v1/noise.glsl>

// ---- Rainbows
//
// A rainbow stands round the point opposite the sun, 42 degrees out from it, red on the outside, as the rain starts
// and for a minute after it stops (while the air is still full of drops and the sun is out again). By night the moon
// makes one the same way, a moonbow: much fainter, and so pale the eye takes it for white. One rain in five or so
// gives a double bow: a fainter second one 51 degrees out, its colours the other way round, and the sky between the
// two a little darker (Alexander's band) than inside the first. Seen by the camera only: a bow lights nothing.

// The colours across a bow, from 0 (its inner, violet edge) to 1 (its outer, red edge).
vec3 aetherSpectrum(float t) {
    float r = exp(-pow((t - 0.86) / 0.20, 2.0)) + 0.30 * exp(-pow((t - 0.04) / 0.10, 2.0));
    float g = exp(-pow((t - 0.55) / 0.20, 2.0));
    float b = exp(-pow((t - 0.20) / 0.17, 2.0));
    return vec3(r, g, b);
}

// How much of a bow the weather allows now, 0..1, and whether this rain's is a double one (seed).
float aetherRainbowWeather(out float seed) {
    float stopped = radianteRainStoppedFor();
    // Just after the rain: in over a few seconds, gone by a minute and a half.
    float after = smoothstep(2.0, 8.0, stopped) * (1.0 - smoothstep(45.0, 90.0, stopped));
    // As it starts: while the air is still dry enough for the sun to come through.
    float starting = smoothstep(0.1, 0.4, radianteRain()) * (1.0 - smoothstep(0.12, 0.35, radianteWetness()));
    // One number for this rain: the moment it stopped, or the half hour it started in.
    seed = after > 0.0 ? floor(radianteTime() - stopped) : floor(radianteTime() / 1800.0) + 0.5;
    return max(after, 0.7 * starting);
}

vec3 aetherRainbow(vec3 dir) {
    float seed;
    float weather = aetherRainbowWeather(seed);
    if (weather <= 0.0 || dir.y <= 0.0) { return vec3(0.0); }
    vec3 sun = radianteSunDirection();
    bool moonbow = sun.y < 0.0;
    vec3 source = moonbow ? radianteMoonDirection() : sun;
    // The bow needs its light low enough to stand over the horizon and above it at all.
    if (source.y < -0.02) { return vec3(0.0); }
    float strength = smoothstep(-0.02, 0.08, source.y) * (1.0 - smoothstep(0.55, 0.75, source.y));
    // By moonlight on the night sky's scale (the reference below follows the sky): fainter, as the full moon is far
    // less bright against its sky than the sun against its own.
    if (moonbow) { strength *= 0.5 * radianteMoonLit(); }
    if (strength <= 0.0) { return vec3(0.0); }
    float angle = acos(clamp(dot(dir, -source), -1.0, 1.0));
    const float PRIMARY = 0.733;   // 42 degrees
    const float SECONDARY = 0.890; // 51 degrees
    const float WIDTH = 0.035;     // about 2 degrees, red to violet
    vec3 light = vec3(0.0);
    float across = (angle - (PRIMARY - WIDTH)) / (2.0 * WIDTH);
    if (across > 0.0 && across < 1.0) { light += aetherSpectrum(across) * sin(across * AETHER_PI); }
    // A faint glow inside the first bow, the light of every drop that sends it back at less than 42 degrees.
    light += vec3(0.10) * smoothstep(PRIMARY - 0.02, PRIMARY - 0.35, angle) * smoothstep(PRIMARY - 0.55, PRIMARY - 0.3, angle);
    bool doubleBow = radianteHash(seed * 0.618) < 0.2;
    if (doubleBow) {
        float outer = (SECONDARY + 1.6 * WIDTH - angle) / (3.2 * WIDTH);
        if (outer > 0.0 && outer < 1.0) { light += 0.4 * aetherSpectrum(outer) * sin(outer * AETHER_PI); }
    }
    // The bow fades towards the horizon's haze and its feet.
    light *= smoothstep(0.0, 0.12, dir.y);
    if (moonbow) { light = mix(light, vec3(aetherLuma(light)), 0.65); }
    return light * (weather * strength);
}

// The bow over everything in the sky, the pack's clouds included: it stands in the rain between the eye and them, so
// it shows against the grey of the passing shower, as a rainbow does. At its brightest about as bright again as the
// sky (aetherSkyReference); seen by the camera only, so it lights nothing.
vec3 aetherRainbowOverSky(vec3 direction) {
    float rainbow = clamp(float(AETHER_RAINBOW), 0.0, 2.0);
    if (rainbow <= 0.0 || !aetherSeen()) { return vec3(0.0); }
    return aetherRainbow(direction) * (1.2 * rainbow * aetherSkyReference(true));
}

#endif
