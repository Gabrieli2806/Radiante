#ifndef RADIANTE_HOOK_CELESTIAL_GLSL
#define RADIANTE_HOOK_CELESTIAL_GLSL

// Hook points (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signatures
// stay as they are within radiante-pt@1.
//
// The sun's and the moon's discs, as every ray that reaches the overworld sky sees them (the camera, reflections and
// bounces alike). Called for every such ray, after hookSky and before the rain hides the discs, their halos are added
// and hookStars and hookCloudLayer cover them. Only how the discs look changes: the sunlight and moonlight the world
// is lit by are sampled apart and stay as they are (hooks/direct_light.glsl changes those).
//   radiance       the disc radiante-pt draws along direction: vanilla's square sprite (the moon's with its phase),
//                  zero off it. Replace it to draw another shape, size or colour
//   direction      world-space ray direction, unit length (y up)
//   sunDirection   towards the sun, unit length; moonDirection towards the moon (its path can differ from the sun's)
//   sunLight       the disc's radiance at full strength for this ray: the sun's brightness, reddened by the air near
//                  the horizon and cut off below it, zero where the ground hides the body. A disc drawn as
//                  sunLight * coverage (0 to 1) is as bright as radiante-pt's own
//   moonLight      the same for the moon, at the brightness radiante-pt shows its face with; skyUBO.moonPhase (0 full
//                  to 4 new) says how much of it is lit
void hookSunDisc(inout vec3 radiance, vec3 direction, vec3 sunDirection, vec3 sunLight) {}
void hookMoonDisc(inout vec3 radiance, vec3 direction, vec3 moonDirection, vec3 moonLight) {}

// Only in this default file: radiante-pt then skips the calls, so its shaders are the same as before the hooks
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_CELESTIAL_DEFAULT 1

#endif
