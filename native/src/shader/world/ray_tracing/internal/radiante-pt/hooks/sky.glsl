#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// Called in the overworld miss shader with the clear sky a ray sees when it leaves the world (the sky cube), before
// rain greys it and before the sun and moon discs, clouds and stars are added. Rays that bounce off the world and
// reach the sky go through here too, so the sky's colour also tints the light it casts.
//   radiance  the sky's radiance along direction
//   direction world-space ray direction, unit length (y up)
void hookSky(inout vec3 radiance, vec3 direction) {}

#endif
