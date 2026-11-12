#ifndef RADIANTE_V1_NOISE_GLSL
#define RADIANTE_V1_NOISE_GLSL

// Noise and a few colour helpers for shader packs. Part of the public pack interface (radiante/v1): functions are
// only added, and what one returns for a given input does not change, so a pack's clouds or grain look the same on
// a later Radiante. Radiante PT itself does not include this file.
//
//   #include <radiante/v1/noise.glsl>
//
// It needs nothing else: no uniform, no texture. Every function is pure, so it works in any stage and any hook.
// Before it existed each pack carried its own copy of these (a hash, value noise, Worley cells, luminance).

// ---- Hashes: a value that looks random for each input, the same every time. Results are in 0..1.

float radianteHash(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float radianteHash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float radianteHash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

vec2 radianteHash2(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.xx + q.yz) * q.zy);
}

vec3 radianteHash3(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

// For a pattern per pixel and per frame (a dither, a film grain): pass the pixel's coordinates and
// radianteFrame() of common/pack_world.glsl.
float radianteHash(uvec2 pixel, uint frame) {
    uint h = pixel.x * 0x9E3779B1u ^ pixel.y * 0x85EBCA77u ^ frame * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return float(h) * (1.0 / 4294967296.0);
}

// ---- Value noise: smooth, 0..1, one feature about every unit of p.

float radianteNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = radianteHash(cell);
    float b = radianteHash(cell + vec2(1.0, 0.0));
    float c = radianteHash(cell + vec2(0.0, 1.0));
    float d = radianteHash(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float radianteNoise(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = radianteHash(cell);
    float n100 = radianteHash(cell + vec3(1.0, 0.0, 0.0));
    float n010 = radianteHash(cell + vec3(0.0, 1.0, 0.0));
    float n110 = radianteHash(cell + vec3(1.0, 1.0, 0.0));
    float n001 = radianteHash(cell + vec3(0.0, 0.0, 1.0));
    float n101 = radianteHash(cell + vec3(1.0, 0.0, 1.0));
    float n011 = radianteHash(cell + vec3(0.0, 1.0, 1.0));
    float n111 = radianteHash(cell + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

// ---- Fractal noise: value noise in octaves, each twice as fine and half as strong, 0..1. One to eight octaves.

float radianteFbm(vec2 p, int octaves) {
    float sum = 0.0;
    float weight = 0.5;
    float total = 0.0;
    for (int i = 0; i < clamp(octaves, 1, 8); ++i) {
        sum += weight * radianteNoise(p);
        total += weight;
        // Turned a little between octaves, so their grids do not line up.
        p = mat2(1.6, 1.2, -1.2, 1.6) * p;
        weight *= 0.5;
    }
    return sum / total;
}

float radianteFbm(vec3 p, int octaves) {
    float sum = 0.0;
    float weight = 0.5;
    float total = 0.0;
    for (int i = 0; i < clamp(octaves, 1, 8); ++i) {
        sum += weight * radianteNoise(p);
        total += weight;
        p = p * 2.02 + vec3(17.3, 5.1, 11.7);
        weight *= 0.5;
    }
    return sum / total;
}

// ---- Cell (Worley) noise: the distance to the nearest of the points scattered one to a unit cell, 0 at a point
// and up to about 1 between them. 1 - radianteWorley(p) gives the round puffs clouds are built from.

float radianteWorley(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    float nearest = 8.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(x, y);
            vec2 toPoint = offset + radianteHash2(cell + offset) - f;
            nearest = min(nearest, dot(toPoint, toPoint));
        }
    }
    return min(sqrt(nearest), 1.0);
}

float radianteWorley(vec3 p) {
    vec3 cell = floor(p);
    vec3 f = fract(p);
    float nearest = 8.0;
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                vec3 offset = vec3(x, y, z);
                vec3 toPoint = offset + radianteHash3(cell + offset) - f;
                nearest = min(nearest, dot(toPoint, toPoint));
            }
        }
    }
    return min(sqrt(nearest), 1.0);
}

// ---- Colour

// How bright a linear colour looks (Rec. 709).
float radianteLuminance(vec3 colour) { return dot(colour, vec3(0.2126, 0.7152, 0.0722)); }

// More or less colourful: 0 grey, 1 as it is, above 1 more so. Never negative.
vec3 radianteSaturate(vec3 colour, float amount) {
    float luminance = radianteLuminance(colour);
    return max(vec3(luminance) + (colour - luminance) * amount, vec3(0.0));
}

// Between linear light and the sRGB encoding of textures and of the screen.
vec3 radianteSrgbToLinear(vec3 srgb) {
    return mix(srgb / 12.92, pow((max(srgb, vec3(0.0)) + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), srgb));
}
vec3 radianteLinearToSrgb(vec3 value) {
    vec3 c = max(value, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

#endif
