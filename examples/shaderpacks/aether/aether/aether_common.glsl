#ifndef AETHER_COMMON_GLSL
#define AETHER_COMMON_GLSL

// Aether: what every hook shares. The weather, the biome and the sun come from radiante-pt's common/pack_world.glsl;
// this adds the pack's time of day (golden hour and blue hour apart from day and night), colour helpers, a hash for
// the stars and the clock. It reads only the world and sky uniforms, which every hook point has (the post grade
// pass included), so any hook can include it.
//
// Brightness rule of the pack: it never invents its own exposure. A colour it paints takes its hue from the pack and
// its brightness from what Radiante PT already has there (aetherAtLuma), so day, dusk and night keep Radiante's
// exposure and the light the sky casts keeps its strength.

#include "common/pack_world.glsl"
#include "aether/aether_settings.glsl"

const float AETHER_PI = 3.14159265;
const vec3 AETHER_LUMA = vec3(0.2126, 0.7152, 0.0722);

float aetherLuma(vec3 c) {
    return dot(c, AETHER_LUMA);
}

// The colour with its hue kept and its luminance set to l.
vec3 aetherAtLuma(vec3 colour, float l) {
    return colour * (l / max(aetherLuma(colour), 1e-6));
}

vec3 aetherSaturate(vec3 c, float amount) {
    return mix(vec3(aetherLuma(c)), c, amount);
}

// The pack's clock in seconds: the wind's, which is game time (it stops with the game) and runs six hours before it
// starts over. worldUBO.gameTime is the day fraction and starts over every twenty minutes.
float aetherSeconds() {
    return worldUBO.windTime;
}

// Henyey-Greenstein phase function, per steradian.
float aetherPhase(float cosTheta, float g) {
    float gg = g * g;
    float denom = max(1.0 + gg - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - gg) / (4.0 * AETHER_PI * denom * sqrt(denom));
}

// The time of day as the sky shows it, each 0 to 1 and adding up to 1, read off the sun's height (it follows the
// Sun Path settings, not the clock):
//   day     the sun more than about 7 degrees up, full from 25
//   golden  the low sun, full from the horizon to 7 degrees: warm light, amber horizon
//   blue    the sun just under the horizon, full from 2 to 6 degrees below: indigo vault, rose horizon, the Belt of
//           Venus opposite the sun
//   night   the sun more than about 6 degrees down, full from 17
// dawn is 1 in the morning half of the day and 0 in the evening one, for a rosier sunrise and an amber sunset.
struct AetherTime {
    float day;
    float golden;
    float blue;
    float night;
    float dawn;
};

AetherTime aetherTime() {
    float h = radianteSunHeight();
    float aboveGolden = smoothstep(0.12, 0.42, h);
    float aboveHorizon = smoothstep(-0.04, 0.04, h);
    float aboveNight = smoothstep(-0.30, -0.10, h);
    AetherTime t;
    t.day = aboveGolden;
    t.golden = aboveHorizon * (1.0 - aboveGolden);
    t.blue = aboveNight * (1.0 - aboveHorizon);
    t.night = 1.0 - aboveNight;
    float angle = radianteSunAngle();
    t.dawn = (angle < 0.25 || angle >= 0.75) ? 1.0 : 0.0;
    return t;
}

// Turns v about a unit axis by an angle (Rodrigues).
vec3 aetherRotate(vec3 v, vec3 axis, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

// A direction in the frame of the night sky: turned back about the sun's path axis by the sun's angle, so the stars
// and the milky way turn with the sun across the night, as the real sky does.
vec3 aetherCelestial(vec3 dir) {
    vec3 axis = skyUBO.celestialAxis.xyz;
    axis = dot(axis, axis) > 1e-8 ? normalize(axis) : vec3(0.0, 0.0, 1.0);
    return aetherRotate(dir, axis, -radianteSunAngle() * 2.0 * AETHER_PI);
}

// A 3D integer hash (PCG3D): three well mixed 32-bit values.
uvec3 aetherHash3(uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

#endif
