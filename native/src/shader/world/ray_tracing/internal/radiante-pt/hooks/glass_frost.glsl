#ifndef RADIANTE_HOOK_GLASS_FROST_GLSL
#define RADIANTE_HOOK_GLASS_FROST_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// How far the frost on windows has grown. Glass and glass panes carry a ragged rim of frost pixels in from their
// edges (laid down when the section is meshed, thinner near a light); each pixel shows once the frost has grown as
// far as its rank. radiante-pt grows it with the snow's wetness, in while it snows and out once it stops.
//   growth    0 none, 1 the whole rim, as radiante-pt would show it: change it to grow the frost at other times (on
//             clear cold nights, before the snow comes) or to keep it from growing at all; clamped to 0..1 after
//             the call. The frost is only there to grow on glass where it snows (a cold biome) and with Snow & Frost
//             on: the rim is meshed there and nowhere else
//   position  the point of the glass hit, camera-relative (worldUBO.cameraPos makes it absolute): growth may vary
//             across a window, or from one window to the next
// Called by the any-hit shaders (world/default.rahit, world/shadow.rahit), so it decides where the frost is seen and
// where it casts its shadow alike; keep it cheap. The weather and the time of day (common/pack_world.glsl) read
// skyUBO, which both declare. The frost's look (its colour, how it shines) is hookSurface's: a frost pixel is told by
// radianteIsGlassFrost(surfaceFlags).
void hookGlassFrost(inout float growth, vec3 position) {}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_GLASS_FROST_DEFAULT 1

#endif
