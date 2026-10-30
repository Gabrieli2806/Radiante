#ifndef AETHER_NOISE_GLSL
#define AETHER_NOISE_GLSL

// Aether: the pack's noise image, textures/aether_noise.png (configs.json, sampled_binding 56), for the ray tracing
// shaders, where a pack's textures are in set 5. 256 x 256, repeating, linear; four channels of smooth noise, each
// spread evenly over 0 to 1 (so a threshold t keeps 1 - t of it), made by filtering white noise in the frequency
// domain, so it tiles without seams:
//   r  large masses (2 to 24 waves across the image): cloud masses, the milky way's clumps, the moon's seas
//   g  billows (5 to 56): cloud billows, puddle shapes
//   b  fine detail (14 to 110): cloud edges, star dust
//   a  streaks along the image's v axis (3 to 40 across, a few along): cirrus, nebula veils
// Sampled at level 0 only: ray tracing shaders have no derivatives, and every use keeps its frequency low enough for
// that.
#include "aether/aether_common.glsl"

layout(set = 5, binding = 56) uniform sampler2D aetherNoise;

vec4 aetherNoiseAt(vec2 uv) {
    return textureLod(aetherNoise, uv, 0.0);
}

#endif
