#ifndef SOBOL_GLSL
#define SOBOL_GLSL

// Owen-scrambled Sobol points, after Burley, "Practical Hash-based Owen Scrambling" (JCGT 9(4), 2020).
//
// The path tracer takes one sample per pixel per frame and the denoisers average it with the pixel's earlier frames.
// Drawn from independent random numbers, a pixel's samples over N frames clump and leave gaps like any random set; drawn
// as the first N points of a (scrambled) Sobol sequence indexed by the frame, they spread evenly over the domain, and
// their average comes closer to the true value for the same number of rays. Each pixel and each sampling decision gets
// its own scramble, so neighbouring pixels stay uncorrelated (the noise between them is unchanged).
//
// A point is made in three steps: the frame index is shuffled by a nested uniform scramble (consecutive frames still
// take a block of consecutive points, but which block differs per pixel and decision), the Sobol point of that index is
// taken in up to three dimensions, and each coordinate is Owen-scrambled with a seed of its own.

// Generator matrices of Sobol dimensions 2 and 3 (Joe and Kuo's primitive polynomials x + 1 and x^2 + x + 1, initial
// numbers 1 and 1, 3), one column per bit of the index, most significant bit first. Dimension 1 is the bit-reversed
// index itself. Sixteen columns serve indices below 65536: frames are counted modulo that (a sequence of over 18
// minutes at 60 frames a second), and the scramble fills in the lower bits of each coordinate.
const uint SOBOL_DIM1[16] = uint[16](
    0x80000000u, 0xc0000000u, 0xa0000000u, 0xf0000000u, 0x88000000u, 0xcc000000u, 0xaa000000u, 0xff000000u,
    0x80800000u, 0xc0c00000u, 0xa0a00000u, 0xf0f00000u, 0x88880000u, 0xcccc0000u, 0xaaaa0000u, 0xffff0000u);
const uint SOBOL_DIM2[16] = uint[16](
    0x80000000u, 0xc0000000u, 0x60000000u, 0x90000000u, 0xe8000000u, 0x5c000000u, 0x8e000000u, 0xc5000000u,
    0x68800000u, 0x9cc00000u, 0xee600000u, 0x55900000u, 0x80680000u, 0xc09c0000u, 0x60ee0000u, 0x90550000u);

// A permutation of 32-bit values in which each bit depends only on itself and the bits below it (every step adds or
// xors a multiple by an even constant, which carries upwards only). Applied to bit-reversed values it permutes from the
// top bit down, which is what an Owen scramble does to the binary digits of a coordinate.
uint sobolLaineKarras(uint x, uint seed) {
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}

uint sobolNestedUniformScramble(uint x, uint seed) {
    return bitfieldReverse(sobolLaineKarras(bitfieldReverse(x), seed));
}

// A well mixed 32-bit integer hash (lowbias32), for the per-coordinate seeds.
uint sobolHash(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

// Point `index` (below 65536) of the three-dimensional Sobol sequence, as 32-bit fixed point. A fixed count of steps
// without branches: the compiler unrolls it and folds the tables into constants (indexed in a loop of varying length,
// they were read from memory, which cost more than the whole sampler saved).
uvec3 sobolPoint3(uint index) {
    uvec3 point = uvec3(bitfieldReverse(index), 0u, 0u);
    for (uint bit = 0u; bit < 16u; bit++) {
        uint take = 0u - ((index >> bit) & 1u);
        point.y ^= SOBOL_DIM1[bit] & take;
        point.z ^= SOBOL_DIM2[bit] & take;
    }
    return point;
}

// Point `index` of the Owen-scrambled, shuffled sequence that `seed` selects, each coordinate in [0, 1). The shuffled
// index keeps to the 65536 points of the first block, which any 2^n consecutive frames of it still stratify.
vec3 owenSobol3(uint index, uint seed) {
    uvec3 point = sobolPoint3(sobolNestedUniformScramble(index & 0xFFFFu, seed) & 0xFFFFu);
    point.x = sobolNestedUniformScramble(point.x, sobolHash(seed ^ 0x9e3779b9u));
    point.y = sobolNestedUniformScramble(point.y, sobolHash(seed ^ 0x7f4a7c15u));
    point.z = sobolNestedUniformScramble(point.z, sobolHash(seed ^ 0x94d049bbu));
    // 24 bits, so that no coordinate rounds up to 1.0 as a float.
    return vec3(point >> 8u) * (1.0 / 16777216.0);
}

#endif // SOBOL_GLSL
