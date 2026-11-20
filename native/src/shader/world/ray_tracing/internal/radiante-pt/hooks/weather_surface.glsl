#ifndef RADIANTE_HOOK_WEATHER_SURFACE_GLSL
#define RADIANTE_HOOK_WEATHER_SURFACE_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// Called in the default closest-hit shader for every surface that is not water, glass or a rain streak, in any
// weather, after radiante-pt has worked out how much rain and snow lie on it and before it lays them on the material
// (pt/pt_material.glsl). A pack changes the amounts to make radiante-pt's effects its own: softer, in other shapes,
// in other places or conditions (wet caves, frost at night, snow in a biome where it never snows), or sets them to 0
// to draw its own in hookSurface instead. Each is 0..1, as radiante-pt would lay it:
//   wetness   darker, glossier ground (and walls the rain runs down)
//   puddle    standing water on the flat top of a full block, with the rain's rings on it
//   frost     a pale film of rime
//   snow      the snow blanket on top (and the band along the top edge of a block's sides)
//   position  the point hit, camera-relative (worldUBO.cameraPos makes it absolute)
//   normal    the face's outward normal
//   surfaceFlags  the packed vertex data: radianteIsLeaves(surfaceFlags), radianteIsPlant... (radiante/v1/surface.glsl)
//   exposure  x: how open the face is to the rain, y: to the snow, both 0..1 and 0 where it does not rain or snow
// Everything radiante-pt's own effects are made of can be called from here: common/rain_wetness.glsl (the column
// data, vptRainFray for a ragged pixel edge, vptSnowSpread and vptSnowCover for how far the snow has settled,
// vptSnowHeatMelt for the melting round fires and lights) and common/pack_world.glsl (the weather, the biome at the
// camera, the time of day). Pixel Rain is VPT_RAIN_PIXELS; RAIN_PIXELS_PER_BLOCK is 16.
void hookWeatherSurface(inout float wetness, inout float puddle, inout float frost, inout float snow, vec3 position,
                        vec3 normal, uint surfaceFlags, vec2 exposure) {}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_WEATHER_SURFACE_DEFAULT 1

#endif
