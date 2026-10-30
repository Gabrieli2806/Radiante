#ifndef RADIANTE_HOOK_DIMENSION_SKY_GLSL
#define RADIANTE_HOOK_DIMENSION_SKY_GLSL

// Hook points (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signatures
// stay as they are within radiante-pt@1.
//
// The sky and the fog of the dimensions without the overworld's sky (the overworld's sky is hooks/sky.glsl's). Which
// dimension it is goes by the sky it has (worldUBO.skyType: 0 none, the Nether's; 2 the End's), so a modded
// dimension with the Nether's or the End's sky goes through the same hooks.
//
// The sky, in the miss shader, for every ray that leaves the world (the camera's, reflections, bounces):
//   radiance  the sky's radiance along direction. The Nether has none, so it arrives as zero there: the light of the
//             sky over the Nether's roof and past the edge of the world. The End's is its violet void with its stars
//             and flash
//   direction world-space ray direction, unit length (y up)
//   seen      hookEndSky only: true for the sky the camera sees (also through glass or water), false for the light
//             the sky casts on the islands, which radiante-pt keeps several times brighter than what is seen
void hookNetherSky(inout vec3 radiance, vec3 direction) {}
void hookEndSky(inout vec3 radiance, vec3 direction, bool seen) {}

// The fog the camera sees the world through (world/world.rgen, once per pixel, with the camera outside water, lava
// and powder snow and without Blindness or Darkness; in the End only over what the view hits, its sky being the
// fog's colour already):
//   colour  the colour things fade into: the dimension's fog colour (the End's turned violet like its sky)
//   density how fast they fade, per block past where the fog starts (two over the fog's length by default)
// Keep the End's fog colour close to what hookEndSky shows near the horizon, or far islands stand out against it.
void hookNetherFog(inout vec3 colour, inout float density) {}
void hookEndFog(inout vec3 colour, inout float density) {}

// Only in this default file: radiante-pt then skips the calls, so its shaders are the same as before the hooks
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_DIMENSION_SKY_DEFAULT 1

#endif
