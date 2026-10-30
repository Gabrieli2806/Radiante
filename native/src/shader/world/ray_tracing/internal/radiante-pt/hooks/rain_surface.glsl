#ifndef RADIANTE_HOOK_RAIN_SURFACE_GLSL
#define RADIANTE_HOOK_RAIN_SURFACE_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signatures
// stay as they are within radiante-pt@1.
//
// Where rain's marks on the ground are worked out: the puddles on open ground and the rings that rain drops leave on
// puddles and water (common/rain_wetness.glsl). radiante-pt's own file only defines RADIANTE_HOOK_RAIN_SURFACE_DEFAULT,
// and with it the hooks below are compiled out. A pack's file leaves that define out and gives
//   vec3 hookRainPosition(vec3 worldPos)  the point the puddle and the rings are looked up at, camera-relative
//                                         (worldPos as the hit has it; return it unchanged to keep radiante-pt's)
//   float hookPuddleEdge(float puddle)    the puddle's cover there, 0..1, from radiante-pt's soft-rimmed value
// Pixelated Lighting snaps the point to the centre of the texture's texel and hardens the rim, so puddles and their
// rings come out in the texture's pixels. Included by common/rain_wetness.glsl, so a pack's file must not need more
// than the world uniform and its own globals.
#define RADIANTE_HOOK_RAIN_SURFACE_DEFAULT

#endif
