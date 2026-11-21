#ifndef RADIANTE_HOOK_FOOTPRINTS_GLSL
#define RADIANTE_HOOK_FOOTPRINTS_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// The footprints players and mobs leave, as radiante-pt is about to lay them (pt/pt_material.glsl, after
// hookWeatherSurface and the snow): the square of each leg pressed where a foot came down, settling back over a few
// minutes. Called for every surface but water and glass; radiante-pt's prints are only on upward faces, so the
// amounts arrive as 0 elsewhere, and a pack that raises them there puts prints there. Each is 0 none to 1 fresh:
//   soft     a step pressed into sand, red sand, mud, clay, soul sand or soul soil: the ground in its own shade
//   packed   snow packed down by a step, greyer and smoother
//   slush    snow and water trodden together, grey-blue and glossy
//   snow     snow carried off on the feet onto a floor
//   wet      water carried off on the feet onto a floor
//   rim      the outer pixels of a print, in the shade of the step (how much the other amounts darken there)
//   position the point hit, camera-relative (worldUBO.cameraPos makes it absolute); radiante-pt reads its prints at
//            vptRainSurfacePosition(position), which a pack's hookRainPosition may move off it
//   surfaceFlags  the packed vertex data: radianteIsLeaves(surfaceFlags)... (radiante/v1/surface.glsl)
// Scale them to make the prints deeper, fainter or gone; each is clamped to 0..1 after the call. A pack that draws
// prints of its own keeps the amounts in globals declared in this file and sets them to 0: hooks/surface.glsl is
// included after it, and hookSurface runs later on the same hit. The snow cleared from a print (where the ground
// shows through) is laid before this hook and keeps radiante-pt's shape; hookWeatherSurface sets how much snow lies
// round it.
void hookFootprints(inout float soft, inout float packed, inout float slush, inout float snow, inout float wet,
                    inout float rim, vec3 position, uint surfaceFlags) {}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_FOOTPRINTS_DEFAULT 1

#endif
