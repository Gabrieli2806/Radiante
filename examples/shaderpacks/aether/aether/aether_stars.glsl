#ifndef AETHER_STARS_GLSL
#define AETHER_STARS_GLSL

// Aether: a star field on the sky, for the overworld's night (hooks/stars.glsl) and the End (hooks/dimension_sky.glsl).
// One candidate star per cell of a grid on the direction; a hash picks whether the cell has one, where in the cell it
// sits (kept off the cell's edges, so it is never cut by its neighbour), how bright it is (most faint, a few bright,
// as magnitudes go), its colour (cool blue-white to orange, from its temperature) and its twinkle. Drawn only where
// the camera looks, so its fine detail never reaches the light the denoiser cleans.
#include "aether/aether_common.glsl"

// The milky way's pole in the night sky's frame (unit length): the stars crowd along the great circle round it
// (hooks/stars.glsl) where the milky way glows (hooks/sky_emission.glsl).
const vec3 AETHER_GALAXY_POLE = vec3(0.4194, 0.5792, 0.6990);

// One layer of stars along a unit direction. cells: grid cells per unit (one cell is about 1/cells radians across);
// chance: the share of cells that hold a star; time: seconds; twinkle: 0 steady to 1 strongly twinkling.
vec3 aetherStarLayer(vec3 dir, float cells, float chance, uint salt, float time, float twinkle) {
    vec3 cell = floor(dir * cells);
    uvec3 h = aetherHash3(uvec3(ivec3(cell) + ivec3(1 << 20)) ^ uvec3(salt, salt * 7u + 1u, salt * 13u + 3u));
    float pick = float(h.x & 0xFFFFu) / 65535.0;
    if (pick > chance) { return vec3(0.0); }
    vec3 jitter = vec3((h.yzx >> 8u) & uvec3(0xFFu)) / 255.0;
    vec3 centre = normalize((cell + 0.25 + 0.5 * jitter) / cells);
    // Distance from the star in cells; the core is a fraction of a cell, a pixel or two at usual fields of view.
    float distance = length(dir - centre) * cells;
    float core = exp(-(distance * distance) / 0.018);
    if (core < 1e-3) { return vec3(0.0); }
    float magnitude = float(h.z & 0xFFFFu) / 65535.0;
    float brightness = 0.12 + 2.6 * pow(magnitude, 7.0);
    float temperature = float((h.y >> 16u) & 0xFFu) / 255.0;
    vec3 colour = temperature < 0.18 ? vec3(1.00, 0.70, 0.48) :
                  temperature < 0.42 ? vec3(1.00, 0.88, 0.74) :
                  temperature < 0.80 ? vec3(0.95, 0.97, 1.00) : vec3(0.68, 0.80, 1.00);
    float phase = float((h.x >> 16u) & 0xFFu) / 255.0 * 6.2831853;
    float flicker = sin(time * (2.3 + 2.9 * temperature) + phase) * sin(time * 1.37 + phase * 2.0);
    return colour * (brightness * core * (1.0 + twinkle * 0.45 * flicker));
}

#endif
