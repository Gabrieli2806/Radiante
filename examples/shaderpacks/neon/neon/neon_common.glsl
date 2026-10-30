#ifndef NEON_COMMON_GLSL
#define NEON_COMMON_GLSL

// Neon Dusk: what the hooks share. The look (day, dusk or night) the sky, the sunlight's tint and the haze are drawn
// in, and where the retro sun stands. Every setting here is a live one ("runtime": true in configs.json): its define
// reads the world uniform, so it is tested with if, never #if. The defaults below are only for a compile outside the
// loader, which always defines them.
//
// The weather and the time of day come from radiante-pt's common/pack_world.glsl.

#include "common/pack_world.glsl"

// 0 the real day and night, 1 a locked dusk, 2 a locked night.
#ifndef NEON_SKY_MODE
#    define NEON_SKY_MODE 1
#endif
#ifndef NEON_SKY_STRENGTH
#    define NEON_SKY_STRENGTH 1.0
#endif
#ifndef NEON_SUN
#    define NEON_SUN 1.0
#endif
#ifndef NEON_SUN_SIZE
#    define NEON_SUN_SIZE 12.0
#endif
#ifndef NEON_SUN_HEIGHT
#    define NEON_SUN_HEIGHT 8.0
#endif

const vec3 NEON_LUMA = vec3(0.2126, 0.7152, 0.0722);

float neonLuminance(vec3 c) {
    return dot(c, NEON_LUMA);
}

// How much the look is day (x), dusk (y) and night (z), adding up to 1. With the real cycle it is the time of day
// read off the sun (radianteDayWeights: sunrise and sunset both count as dusk); locked, it is one look all day. Only
// what things look like follows it: the sun and moon still light the world from where they really are.
vec3 neonLookWeights() {
    int mode = int(NEON_SKY_MODE);
    if (mode == 1) { return vec3(0.0, 1.0, 0.0); }
    if (mode == 2) { return vec3(0.0, 0.0, 1.0); }
    vec4 weights = radianteDayWeights();
    return vec3(weights.y, weights.x + weights.z, weights.w);
}

// Where the sun sets, flat on the horizon: a quarter turn along the sun's path from its noon (as radianteSunAngle
// measures it), so it follows the Sun Path settings. West with vanilla's path.
vec3 neonSunsetDirection() {
    vec3 axis = skyUBO.celestialAxis.xyz;
    axis = dot(axis, axis) > 1e-8 ? normalize(axis) : vec3(0.0, 0.0, 1.0);
    vec3 noon = vec3(0.0, 1.0, 0.0) - axis * axis.y;
    noon = dot(noon, noon) > 1e-6 ? normalize(noon) : vec3(0.0, 1.0, 0.0);
    vec3 evening = cross(axis, noon);
    vec2 flat2 = evening.xz;
    return dot(flat2, flat2) > 1e-6 ? normalize(vec3(flat2.x, 0.0, flat2.y)) : vec3(-1.0, 0.0, 0.0);
}

// The centre of the retro sun the camera sees. With the real cycle it is the real sun. Locked, it stands low over
// where the sun sets, NEON_SUN_HEIGHT degrees up, all day: a picture in the sky, not the light. The sunlight still
// comes from Radiante's real sun (its direction, shadows and highlights), so at noon the shadows fall straight down
// under a sun drawn on the horizon. Faking the light's direction would light the world from where nothing is.
vec3 neonSunCentre() {
    if (int(NEON_SKY_MODE) == 0) { return radianteSunDirection(); }
    float elevation = radians(float(NEON_SUN_HEIGHT));
    return normalize(neonSunsetDirection() * cos(elevation) + vec3(0.0, sin(elevation), 0.0));
}

#endif
