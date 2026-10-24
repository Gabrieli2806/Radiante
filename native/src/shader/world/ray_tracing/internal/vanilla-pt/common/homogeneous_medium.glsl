#ifndef VPT_HOMOGENEOUS_MEDIUM_GLSL
#define VPT_HOMOGENEOUS_MEDIUM_GLSL

// A stretch of a medium of even density (lava, powder snow, water): along it the light from behind is absorbed per
// colour channel, and the medium adds light of its own. What that light is depends on the medium and is the caller's
// to say: lava's glow, or the light powder snow or water scatter towards the viewer. Either way it is a source of
// constant radiance spread along the stretch, so one closed form covers them all; a medium whose density varies from
// place to place (clouds, haze by height) needs a march instead.
//
// distance        how far the ray travels inside the medium, in blocks
// sigmaT          extinction per block, per channel: how much light the medium takes out
// sourceRadiance  the radiance the medium shows where it is too thick to see through
// transmittance   how much of the light from behind the stretch gets through
// inScatter       the light the medium itself adds along the stretch
void homogeneousMediumSegment(float distance, vec3 sigmaT, vec3 sourceRadiance, out vec3 transmittance,
                              out vec3 inScatter) {
    transmittance = exp(-max(sigmaT, vec3(0.0)) * max(distance, 0.0));
    inScatter = max(sourceRadiance, vec3(0.0)) * (vec3(1.0) - transmittance);
}

#endif
