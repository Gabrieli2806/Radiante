#ifndef RADIANTE_HOOK_STARS_GLSL
#define RADIANTE_HOOK_STARS_GLSL

// Neon Dusk: a few stars in white, pink and cyan over the night look (and fainter in the upper dusk sky), so a locked
// night still has stars at noon, when vanilla's are not drawn. The sky the camera sees only (radiante-pt calls this
// hook for no other ray). Fixed to the sky, one candidate per cell of a grid on the direction, each about three
// pixels across, with no twinkle: nothing changes from frame to frame. Radiante's own stars are kept.
#ifndef NEON_STARS
#    define NEON_STARS 0.4
#endif

#include "neon/neon_sky.glsl"

float hookStars(inout vec3 radiance, vec3 direction) {
    float stars = max(float(NEON_STARS), 0.0) * clamp(float(NEON_SKY_STRENGTH), 0.0, 1.0);
    if (stars <= 0.0 || direction.y <= 0.05) { return 1.0; }
    vec3 w = neonLookWeights();
    float night = w.z + 0.3 * w.y;
    if (night <= 0.0) { return 1.0; }

    const float CELLS = 260.0;
    vec3 cell = floor(direction * CELLS);
    uvec3 h3 = uvec3(ivec3(cell) + ivec3(1 << 20));
    uint h = h3.x * 73856093u ^ h3.y * 19349663u ^ h3.z * 83492791u;
    h ^= h >> 15u;
    h *= 2246822519u;
    h ^= h >> 13u;
    if (float(h & 0xFFFFu) / 65535.0 < 0.9965) { return 1.0; }
    vec3 centre = normalize((cell + 0.5) / CELLS);
    float spot = smoothstep(0.45, 0.0, length(direction - centre) * CELLS);
    uint kind = (h >> 16u) % 3u;
    vec3 colour = kind == 0u ? vec3(1.0) : kind == 1u ? vec3(1.0, 0.45, 0.85) : vec3(0.45, 0.9, 1.0);
    float brightness = 0.6 + 0.8 * float((h >> 20u) & 0xFFu) / 255.0;
    float fade = smoothstep(0.05, 0.35, direction.y) * (1.0 - radianteRain());
    radiance += colour * (spot * brightness * night * fade * stars * 1.5 * neonSkyReference());
    return 1.0;
}

#endif
