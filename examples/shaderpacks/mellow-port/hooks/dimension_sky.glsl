#ifndef RADIANTE_HOOK_DIMENSION_SKY_GLSL
#define RADIANTE_HOOK_DIMENSION_SKY_GLSL

// Mellow port: Mellow's End sky and the End fog's colour. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW):
// get_end_sky in shaders/global/atmosphere/sky.glsl (a dark violet top, two slowly turning layers of green and orange
// "End lights" round the horizon from fbm_fast in shaders/lib/noise.glsl, brighter towards the End flash, and the
// void falling to black below) with the default scheme's f_END_SKY_T and f_END_AURORA colours (shaders/lib/
// colors.glsl). Its brightness is Radiante's: Mellow's top colour is brought to the brightness of Radiante's End sky
// (the dimension's fog colour, seen at END_VOID_SEEN of the light it casts), so the islands are lit as before. It
// replaces Radiante's End sky, its sparse stars included. The fog the islands fade into takes Mellow's top colour at
// Radiante's brightness, so far islands melt into the sky. The Nether keeps Radiante's. End Sky sets how much.
#ifndef MELLOW_END_SKY
#    define MELLOW_END_SKY 1.0
#endif

#include "mellow/mellow_clouds.glsl"

const vec3 MELLOW_LUMINANCE = vec3(0.2126, 0.7152, 0.0722);

// Mellow's fbm_fast: the noise image's red channel over a few octaves, each turned half a radian.
float mellowFbmFast(vec2 x, int detail) {
    float v = 0.0;
    float a = 0.5;
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    for (int i = 0; i < detail; ++i) {
        v += a * textureLod(mellowNoiseTex, x / MELLOW_NOISE_RES, 0.0).x;
        x = rot * x * 2.0 + vec2(100.0);
        a *= 0.5;
    }
    return v;
}

vec2 mellowRotate(vec2 p, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec2(p.x * c - p.y * s, p.y * c + p.x * s);
}

float mellowFogify(float x, float w) {
    return w / (x * x + w);
}

// f_END_SKY_T, Mellow's End sky overhead.
vec3 mellowEndTop() {
    return mellowToLinear(vec3(0.15, 0.1, 0.2));
}

// get_end_sky in Mellow's own units.
vec3 mellowEndSky(vec3 dir) {
    vec3 lights1 = mellowToLinear(vec3(0.0, 0.45, 0.25));
    vec3 lights2 = mellowToLinear(vec3(0.45, 0.3, 0.1));
    float seconds = mod(worldUBO.gameTime * 1200.0, 3600.0 * 12.0);
    float upDot = dir.y;
    vec2 rot1 = mellowRotate(dir.xz, seconds * 0.02);
    vec2 rot2 = mellowRotate(dir.xz, -seconds * 0.007);
    float noise1 = mellowFbmFast(rot1 * 160.0, 2);
    float noise2 = mellowFbmFast(rot2 * 280.0, 2);
    vec3 lights = (lights1 * noise1 + lights2 * noise2) * (1.0 - abs(upDot));
    // The End flash (endFlashPosition): skyUBO.sunDirection points at it in the End, its length the flash's strength.
    float flash = clamp(length(skyUBO.sunDirection), 0.0, 1.0);
    if (flash > 0.0) {
        vec3 flashDir = skyUBO.sunDirection / max(flash, 1e-6);
        vec2 flashXZ = dot(flashDir.xz, flashDir.xz) > 1e-6 ? normalize(flashDir.xz) : vec2(0.0);
        lights *= 1.0 + max(0.0, 5.0 - distance(dir.xz, flashXZ) * 8.0) * flash;
    }
    vec3 sky = mellowEndTop() + lights * mellowFogify(upDot + 0.2, 0.05);
    return sky * (1.0 - mellowFogify(max(upDot + 0.2, 0.0), 0.02));
}

// The brightness Radiante's own End sky has at the horizon: its fog colour's (END_VOID_TINT is about as bright as
// white), a little over a sixth of that where the camera looks (END_VOID_SEEN 0.15).
float mellowEndBrightness(bool seen) {
    return dot(max(worldUBO.fogColor.rgb, vec3(0.0)), MELLOW_LUMINANCE) * (seen ? 0.15 : 1.0);
}

void hookNetherSky(inout vec3 radiance, vec3 direction) {}

void hookEndSky(inout vec3 radiance, vec3 direction, bool seen) {
    if (float(MELLOW_END_SKY) <= 0.0) return;
    vec3 mellow = mellowEndSky(direction) * (mellowEndBrightness(seen) / dot(mellowEndTop(), MELLOW_LUMINANCE));
    radiance = mix(radiance, mellow, float(MELLOW_END_SKY));
}

void hookNetherFog(inout vec3 colour, inout float density) {}

void hookEndFog(inout vec3 colour, inout float density) {
    vec3 top = mellowEndTop();
    vec3 mellow = top * (dot(colour, MELLOW_LUMINANCE) / dot(top, MELLOW_LUMINANCE));
    colour = mix(colour, mellow, float(MELLOW_END_SKY));
}

#endif
