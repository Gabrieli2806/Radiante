#ifndef RADIANTE_HOOK_WATER_WAVES_GLSL
#define RADIANTE_HOOK_WATER_WAVES_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// Shapes radiante-pt's own water waves: the Realistic water surface (common/fft_water.glsl) and the Bedrock one
// (common/bedrock_water.glsl), where the surface's normal is bent and, for Realistic water, where the caustics on the
// bottom are worked out (world/shadow.rahit), so the light under the water follows the same waves. Vanilla water has
// no waves to shape; a pack draws its own there with hooks/water_surface.glsl. Each value arrives as 1:
//   strength how steep the waves are (the normal's tilt, and the wave height hookWaterSurface is given): 0 is flat
//            water, 2 twice as rough. On top of the Water Wave Strength setting
//   speed    how fast they move: 0 stops them, 2 is twice as fast
//   size     how long they are: 2 makes every wave twice as long and wide, 0.5 half
// worldUBO and skyUBO are available (common/pack_world.glsl gives the weather), but nothing about the point shaded:
// the same values hold for all the water in view.
void hookWaterWaves(inout float strength, inout float speed, inout float size) {}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_WATER_WAVES_DEFAULT 1

#endif
