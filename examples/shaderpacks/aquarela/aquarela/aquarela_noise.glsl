#ifndef AQUARELA_NOISE_GLSL
#define AQUARELA_NOISE_GLSL

// Aquarela's noise, shared by its sky hook and its post passes. Integer hashes (pcg3d, Jarzynski and Olano 2020)
// rather than sin() hashes, so a pattern is the same on every GPU and does not break up into bands far from the
// origin. Everything here is a pure function of its input: the post passes feed it pixel positions, so the paper and
// the brush marks stay fixed to the screen, and the sky feeds it the ray's direction, so its washes stay in the sky.

uvec3 aquarelaPcg3d(uvec3 v) {
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

// 0..1 per cell. The offset keeps the cells of negative coordinates apart from the positive ones.
float aquarelaRandom(ivec2 cell, uint seed) {
    return float(aquarelaPcg3d(uvec3(cell + ivec2(32768), seed)).x >> 8u) * (1.0 / 16777216.0);
}

vec2 aquarelaRandom2(ivec2 cell, uint seed) {
    uvec3 h = aquarelaPcg3d(uvec3(cell + ivec2(32768), seed));
    return vec2(h.xy >> 8u) * (1.0 / 16777216.0);
}

float aquarelaRandom3(ivec3 cell) {
    return float(aquarelaPcg3d(uvec3(cell + ivec3(32768))).x >> 8u) * (1.0 / 16777216.0);
}

// Smooth value noise, 0..1, one random value per unit cell.
float aquarelaValueNoise(vec2 p, uint seed) {
    vec2 cell = floor(p);
    vec2 f = p - cell;
    vec2 u = f * f * (3.0 - 2.0 * f);
    ivec2 c = ivec2(cell);
    float a = aquarelaRandom(c, seed);
    float b = aquarelaRandom(c + ivec2(1, 0), seed);
    float d = aquarelaRandom(c + ivec2(0, 1), seed);
    float e = aquarelaRandom(c + ivec2(1, 1), seed);
    return mix(mix(a, b, u.x), mix(d, e, u.x), u.y);
}

// A few octaves of it, 0..1, each half the size and half the weight of the one before.
float aquarelaFbm(vec2 p, int octaves, uint seed) {
    float sum = 0.0;
    float weight = 0.5;
    float total = 0.0;
    for (int i = 0; i < octaves; i++) {
        sum += aquarelaValueNoise(p, seed + uint(i) * 7919u) * weight;
        total += weight;
        p = p * 2.03 + vec2(17.1, 5.3);
        weight *= 0.5;
    }
    return sum / total;
}

// The same in three dimensions, for directions in the sky.
float aquarelaValueNoise3(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = p - cell;
    vec3 u = f * f * (3.0 - 2.0 * f);
    ivec3 c = ivec3(cell);
    float x00 = mix(aquarelaRandom3(c), aquarelaRandom3(c + ivec3(1, 0, 0)), u.x);
    float x10 = mix(aquarelaRandom3(c + ivec3(0, 1, 0)), aquarelaRandom3(c + ivec3(1, 1, 0)), u.x);
    float x01 = mix(aquarelaRandom3(c + ivec3(0, 0, 1)), aquarelaRandom3(c + ivec3(1, 0, 1)), u.x);
    float x11 = mix(aquarelaRandom3(c + ivec3(0, 1, 1)), aquarelaRandom3(c + ivec3(1, 1, 1)), u.x);
    return mix(mix(x00, x10, u.y), mix(x01, x11, u.y), u.z);
}

float aquarelaFbm3(vec3 p, int octaves) {
    float sum = 0.0;
    float weight = 0.5;
    float total = 0.0;
    for (int i = 0; i < octaves; i++) {
        sum += aquarelaValueNoise3(p) * weight;
        total += weight;
        p = p * 2.03 + vec3(17.1, 5.3, 11.7);
        weight *= 0.5;
    }
    return sum / total;
}

// Distance to the nearest of points scattered one per unit cell (Worley's F1), about 0..0.8.
float aquarelaCells(vec2 p, uint seed) {
    vec2 cell = floor(p);
    vec2 f = p - cell;
    ivec2 c = ivec2(cell);
    float nearest = 8.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 point = vec2(x, y) + aquarelaRandom2(c + ivec2(x, y), seed);
            vec2 d = point - f;
            nearest = min(nearest, dot(d, d));
        }
    }
    return sqrt(nearest);
}

#endif
