#ifndef RANDOM_GLSL
#define RANDOM_GLSL

// Hashing and pseudo-random numbers for the path tracer.
//
// Two building blocks: xxhash32 turns a few integers (pixel, frame) into a well mixed 32-bit seed once per pixel,
// and pcg/rand then step that seed along to draw as many uniform numbers as a path needs. Every call site relies on
// the exact sequence these produce (the denoisers and the radiance cache expect the same draws frame to frame), so
// the constants and the order of operations below must not change.

precision highp float;

// Mixes a 96-bit key (for example pixel x, pixel y and the frame number) into one 32-bit value. It is the
// 32-bit xxHash finaliser applied to three lanes: slower than a single pcg step but far better at hiding patterns
// between neighbouring keys, which is what a per-pixel seed needs.
uint xxhash32(uvec3 key) {
    const uvec4 kPrime = uvec4(2246822519U, 3266489917U, 668265263U, 374761393U);

    // Fold z and x in, rotate left by 17, scramble; then the same for y.
    uint acc = key.z + kPrime.w + key.x * kPrime.y;
    acc = kPrime.z * ((acc << 17) | (acc >> (32 - 17)));
    acc += key.y * kPrime.y;
    acc = kPrime.z * ((acc << 17) | (acc >> (32 - 17)));

    // Avalanche: xor-shift / multiply rounds so every input bit reaches every output bit.
    acc = kPrime.x * (acc ^ (acc >> 15));
    acc = kPrime.y * (acc ^ (acc >> 13));
    return acc ^ (acc >> 16);
}

// One step of a 32-bit permuted congruential generator. The state advances with a plain LCG; the value handed back
// is the state run through a data-dependent shift and a multiply, which removes the weak low bits an LCG has.
uint pcg(inout uint state) {
    uint advanced = state * 747796405u + 2891336453u;
    uint permuted = ((advanced >> ((advanced >> 28u) + 4u)) ^ advanced) * 277803737u;
    state = advanced;
    return (permuted >> 22u) ^ permuted;
}

// A uniform float in [0, 1] drawn from (and advancing) the given seed.
float rand(inout uint seed) {
    uint bits = pcg(seed);
    return float(bits) * (1.F / float(0xffffffffu));
}

#endif // RANDOM_GLSL
