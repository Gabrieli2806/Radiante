#ifndef PT_SAMPLING_GLSL
#define PT_SAMPLING_GLSL

// The random numbers of the path's main sampling decisions, low-discrepancy across frames (util/sobol.glsl): at each
// bounce, the BSDF's lobe and direction, and the direction towards the sun or moon disc. Indexed by the frame, they
// stratify each pixel's samples over the frames the denoisers average, where independent numbers clump. The rest of
// the path (light list picks, Russian roulette, media) keeps drawing from the ray's seed.
//
// VPT_LOW_DISCREPANCY 0 (the renderer sets it from RADIANTE_NO_SOBOL, for comparisons) draws these from the seed too.

#include "util/sobol.glsl"

#ifndef VPT_LOW_DISCREPANCY
#    define VPT_LOW_DISCREPANCY 1
#endif

const uint PT_SAMPLE_BSDF = 0u;
const uint PT_SAMPLE_CELESTIAL = 1u;

// A point for one decision at the ray's current bounce. The pixel, the decision, the bounce and whether the path fills
// the radiance cache pick the scramble; the frame picks the point.
vec3 ptSamplePoint(MainRay ray, uint decision) {
    uint pixel = gl_LaunchIDEXT.x | (gl_LaunchIDEXT.y << 16u);
    uint stream = rayCacheUpdate(ray) ? 0x51633e2du : 0x2545f491u;
    uint scramble = xxhash32(uvec3(pixel, (rayBounce(ray) << 4u) | decision, stream));
    return owenSobol3(worldUBO.frameCounter, scramble);
}

// Three numbers for DisneySampleXi: the first two (the best stratified pair) for the direction, the third for the lobe.
vec3 ptBsdfSample(inout MainRay ray) {
#if VPT_LOW_DISCREPANCY
    return ptSamplePoint(ray, PT_SAMPLE_BSDF);
#else
    float xiA = rand(ray.seed);
    float xiB = rand(ray.seed);
    float xiLobe = rand(ray.seed);
    return vec3(xiA, xiB, xiLobe);
#endif
}

// Two numbers for SampleVMFXi towards the sun or moon.
vec2 ptCelestialSample(inout MainRay ray) {
#if VPT_LOW_DISCREPANCY
    return ptSamplePoint(ray, PT_SAMPLE_CELESTIAL).xy;
#else
    float xiCos = rand(ray.seed);
    float xiAngle = rand(ray.seed);
    return vec2(xiCos, xiAngle);
#endif
}

#endif // PT_SAMPLING_GLSL
