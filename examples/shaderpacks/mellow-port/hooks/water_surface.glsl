#ifndef RADIANTE_HOOK_WATER_SURFACE_GLSL
#define RADIANTE_HOOK_WATER_SURFACE_GLSL

// Mellow port: Mellow's water normals (its WATER_NORMALS setting, on by default). Ported from Mellow by TheCMK (MIT,
// LICENSE-MELLOW): noise_water in shaders/lib/noise.glsl and get_water_normal in shaders/global/water.glsl: two layers
// of the normal stored in noisetex's green and blue, scrolling against each other, at 1/24 and 1/64 of a block per
// texel repeat. On the top of water they take the place of Radiante's own waves; the sides keep Radiante's, as
// Mellow's flowing sides would need the flow direction, which Radiante does not pass. The caustics under the water
// stay Radiante's, shaped by the same size, speed and strength (hooks/water_waves.glsl).
#ifndef MELLOW_WATER_NORMALS
#    define MELLOW_WATER_NORMALS 1
#endif

#include "hooks/water_waves.glsl"
#include "mellow/mellow_clouds.glsl"

// noise_water: frameTimeCounter is the world's clock in seconds; the scroll is folded into the texture's repeat
// (24 and 64 blocks) so the coordinates stay precise as the clock grows.
vec2 mellowWaterNoise(vec2 coords) {
    float seconds = worldUBO.gameTime * 1200.0;
    coords /= float(MELLOW_WATER_NORMAL_SIZE);
    float slow = mod(seconds * 0.2 * float(MELLOW_WATER_NORMAL_SPEED), 24.0);
    float fast = mod(seconds * 0.8 * float(MELLOW_WATER_NORMAL_SPEED), 64.0);
    vec2 color = (textureLod(mellowNoiseTex, (coords + slow) / 24.0, 0.0).yz * 2.0 - 1.0) * 0.05;
    color += (textureLod(mellowNoiseTex, (coords - fast) / 64.0, 0.0).yz * 2.0 - 1.0) * 0.1;
    return color * float(MELLOW_WATER_NORMAL_STRENGTH);
}

void hookWaterSurface(inout SampledSurface surface, vec3 position, float waveHeight) {
#if MELLOW_WATER_NORMALS
    if (abs(surface.geometricNormal.y) < 0.75) return;
    // get_water_normal: the noise as the tangent-space slope, its z what is left of a unit vector; the top of water
    // has x and z as its tangents, so y is up. Seen from under the water the normal points down.
    vec2 n = mellowWaterNoise(position.xz);
    vec3 normal = normalize(vec3(n.x, 1.0 - dot(n, n), n.y));
    if (surface.geometricNormal.y < 0.0) normal.y = -normal.y;
    surface.shadingNormal = normal;
#endif
}

#endif
