#ifndef RADIANTE_HOOK_FALLING_WEATHER_GLSL
#define RADIANTE_HOOK_FALLING_WEATHER_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// The look of falling rain and snow. radiante-pt draws Minecraft's own sheets of them, and the view passes through a
// streak or a flake (pt/pt_glass.glsl), which adds a little light and hides a little of what lies behind it. What the
// camera sees of them is laid over the finished picture after the denoiser and the upscaler
// (common/weather_layer.glsl), so they never smear or leave trails; light and shadows pass them by.
//   light     the light the streak or the flake adds, before what lies behind it. radiante-pt's: for rain the sky
//             above, greyed by the storm, times the streak's coverage and the Rain Streak Brightness setting; for snow
//             the flake's colour lit by that sky, times its coverage
//   tint      what it lets through of the view behind it, per colour, 1 all of it. radiante-pt's: for rain a little
//             blue-grey by the coverage and the Rain Opacity setting; for snow most of the view behind a flake hidden.
//             Over the picture the three are taken as their mean
//   coverage  how much of this pixel the texture covers, 0..1
//   position  the point hit, camera-relative (worldUBO.cameraPos makes it absolute)
//   snow      a snowflake rather than a rain streak
// The splashes rain throws up are radianteIsSplash surfaces, not this. The weather (common/pack_world.glsl) is
// readable here. Clamped after the call: light to 0 and up, tint to 0..1.
void hookFallingWeather(inout vec3 light, inout vec3 tint, float coverage, vec3 position, bool snow) {}

// Only in this default file: radiante-pt then skips the call. A pack's file must not define it.
#define RADIANTE_HOOK_FALLING_WEATHER_DEFAULT 1

#endif
