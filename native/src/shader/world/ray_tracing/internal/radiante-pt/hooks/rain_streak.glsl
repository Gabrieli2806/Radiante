#ifndef RADIANTE_HOOK_RAIN_STREAK_GLSL
#define RADIANTE_HOOK_RAIN_STREAK_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// The look of falling rain. radiante-pt draws Minecraft's own rain sheets, and the view passes through a streak
// (pt/pt_glass.glsl), taking a little of what lies behind it and adding a faint glint of the sky:
//   light     the light the streak adds, before what lies behind it: radiante-pt's is the sky above, greyed by the
//             storm, times the streak's coverage and the Rain Streak Brightness setting
//   tint      what the streak lets through of the view behind it, per colour: radiante-pt's is a little blue-grey,
//             by the coverage and the Rain Opacity setting (1 lets everything through)
//   coverage  how much of this pixel the streak's texture covers, 0..1
//   position  the point of the streak hit, camera-relative (worldUBO.cameraPos makes it absolute)
// Only rain: the camera sees it (also through glass and water), light and shadows pass it by. Falling snow is a
// surface shaded as any other, which hookSurface tells by radianteIsSnowfall(surfaceFlags); the splashes rain throws
// up are radianteIsSplash. The weather (common/pack_world.glsl) is readable here.
void hookRainStreak(inout vec3 light, inout vec3 tint, float coverage, vec3 position) {}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_RAIN_STREAK_DEFAULT 1

#endif
