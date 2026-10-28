#ifndef RADIANTE_HOOK_WATER_GLSL
#define RADIANTE_HOOK_WATER_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// Called for the water a ray crosses (waterMediumExtinction in common/water_medium.glsl), with the water's
// extinction per colour channel, per block, before the density settings scale it. Larger values absorb that colour
// faster: raise red to make deep water bluer and teal sooner. skyUBO is available.
void hookWaterAbsorption(inout vec3 extinction) {}

#endif
