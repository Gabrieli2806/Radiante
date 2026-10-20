#ifndef VPT_BEDROCK_ATMOSPHERE_GLSL
#define VPT_BEDROCK_ATMOSPHERE_GLSL

// The moon's own direction for the glow it gives the sky by night.
#include "common/celestial.glsl"

// The Bedrock style atmosphere (VPT_BEDROCK_ATMOSPHERE): Bedrock RTX does not scatter light through an atmosphere, it
// looks its sky and its sunlight up in two small tables, and these are them, read from the player's own Bedrock
// install (configs.json, bedrock_sky and bedrock_luts; they cannot ship in the pack). Without them the textures are
// 1 x 1 stand-ins and the Java style sky is used.
//
// sky.png, 64 x 256: eight strips of 32 rows, one per sun height (0 the sun overhead, 3 on the horizon, 7 midnight).
// Across a strip, the direction's angle from the sun's around the horizon (left towards the sun); down it, the
// direction's height from the zenith to straight down, the horizon at the middle.
// look_up_tables.png, 128 x 4: across, the sun's angle from the zenith (the horizon at the middle); row 0 the colour
// of the sunlight, row 1 its strength.
//
// The experimental Bedrock profile (VPT_BEDROCK_ORIGINAL) reads them as Bedrock RTX itself does, with the values the
// game hands its shaders each frame (calibrated to Bedrock RTX at every hour of the day): both tables are looked up
// at w = acos(sun height) / pi, the lookup table end to end over its 128 texels; the sky takes strip 7w, blending the
// two nearest; the light of the sun is 2 x row 0 squared, and by night, past the table's middle, the same row gives
// the moon's; the sky is scaled by 0.85^2 x row 1 squared (0.72 by day, 0.018 at night).
layout(set = 5, binding = 37) uniform sampler2D bedrockSkyTexture;
layout(set = 5, binding = 38) uniform sampler2D bedrockLutTexture;

#ifndef VPT_BEDROCK_ATMOSPHERE
#    define VPT_BEDROCK_ATMOSPHERE 0
#endif
#ifndef VPT_BEDROCK_SKY_INTENSITY
#    define VPT_BEDROCK_SKY_INTENSITY 1.0
#endif

bool bedrockAtmosphereActive() {
    return VPT_BEDROCK_ATMOSPHERE != 0 && textureSize(bedrockSkyTexture, 0) == ivec2(64, 256) &&
           textureSize(bedrockLutTexture, 0).x == 128;
}

vec3 bedrockSrgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

// Where the time of day is in both tables: 0 with the sun overhead, a half on the horizon, 1 at midnight.
float bedrockDayCoord(float sunHeight) {
    return acos(clamp(sunHeight, -1.0, 1.0)) / PI;
}

// Which strip, fractional, for a sun this high.
float bedrockSkyStrip(float sunHeight) {
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    return 7.0 * bedrockDayCoord(sunHeight);
#else
    // Overhead to the horizon over strips 0 to 3, then down to the antipode over 3 to 7.
    float elevation = asin(clamp(sunHeight, -1.0, 1.0)) / (0.5 * PI);
    return elevation >= 0.0 ? (1.0 - elevation) * 3.0 : 3.0 + min(-elevation, 1.0) * 4.0;
#endif
}

// One row of the lookup table at this time of day, as the texture holds it.
vec4 bedrockLutRow(float sunHeight, int row) {
    return texture(bedrockLutTexture, vec2((bedrockDayCoord(sunHeight) * 127.0 + 0.5) / 128.0, (float(row) + 0.5) / 4.0));
}

// Bedrock RTX's light of the sun, or by night of the moon, on a surface facing it: 2 x row 0 squared (2 at noon,
// about 0.0005, 0.0011, 0.0025 at midnight).
vec3 bedrockCelestialLight(float sunHeight) {
    vec3 colour = bedrockLutRow(sunHeight, 0).rgb;
    return 2.0 * colour * colour;
}

// Bedrock RTX's scale on its sky: 0.85^2 x row 1 squared.
float bedrockSkyIntensityAdjustment(float sunHeight) {
    float strength = bedrockLutRow(sunHeight, 1).r;
    return 0.7225 * strength * strength;
}

vec3 bedrockSkyStripSample(float strip, float across, float down) {
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    vec2 uv = vec2((across * 63.0 + 0.5) / 64.0, (strip * 32.0 + down * 31.0 + 0.5) / 256.0);
#else
    vec2 uv = vec2((across * 63.0 + 0.5) / 64.0, (strip * 32.0 + clamp(down * 32.0, 0.5, 31.5)) / 256.0);
#endif
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    return textureLod(bedrockSkyTexture, uv, 0.0).rgb;
#else
    return texture(bedrockSkyTexture, uv).rgb;
#endif
}

// The sky's radiance in this direction, in the units of the Java sky (VPT_BEDROCK_SKY_INTENSITY scales it).
vec3 bedrockSkyRadiance(vec3 dir, vec3 sunDir) {
    dir = normalize(dir);
    vec2 flatDir = dir.xz;
    vec2 flatSun = sunDir.xz;
    float across = 0.0;
    if (dot(flatDir, flatDir) > 1e-8 && dot(flatSun, flatSun) > 1e-8) {
        across = acos(clamp(dot(normalize(flatDir), normalize(flatSun)), -1.0, 1.0)) / PI;
    }
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    float azimuth = dot(flatDir, flatDir) > 1e-8 ? atan(dir.z, dir.x) : 0.0;
    float sunAzimuth = dot(flatSun, flatSun) > 1e-8 ? atan(sunDir.z, sunDir.x) : 0.0;
    across = abs(2.0 * fract((azimuth - sunAzimuth) / (2.0 * PI)) - 1.0);
#endif
    float down = 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / PI;
    float strip = bedrockSkyStrip(sunDir.y);
    float first = floor(strip);
    vec3 a = bedrockSkyStripSample(min(first, 7.0), across, down);
    vec3 b = bedrockSkyStripSample(min(first + 1.0, 7.0), across, down);
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // And the glow around the sun (the moon by night): 0.05 x max(cos, 0)^7 of the light's colour, part of the sky
    // that lights the world as much as of the one the camera sees.
    vec3 toLight = sunDir.y >= 0.0 ? sunDir : celestialMoonDirection();
    vec3 glow = 0.05 * pow(max(dot(dir, normalize(toLight)), 0.0), 7.0) * bedrockCelestialLight(sunDir.y);
    return (mix(a * a, b * b, strip - first) + glow) * bedrockSkyIntensityAdjustment(sunDir.y) *
           VPT_BEDROCK_SKY_INTENSITY;
#else
    return bedrockSrgbToLinear(mix(a, b, strip - first)) * VPT_BEDROCK_SKY_INTENSITY;
#endif
}

// The sunlight's colour and strength for a sun this high, as a factor on the sun's radiance.
vec3 bedrockSunlight(float sunHeight) {
    float x = acos(clamp(sunHeight, -1.0, 1.0)) / PI;
    float u = (x * 127.0 + 0.5) / 128.0;
    vec3 colour = bedrockSrgbToLinear(texture(bedrockLutTexture, vec2(u, 0.125)).rgb);
    float strength = texture(bedrockLutTexture, vec2(u, 0.375)).r;
    return colour * strength;
}

#endif
