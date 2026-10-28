#ifndef RADIANTE_HOOK_FOG_GLSL
#define RADIANTE_HOOK_FOG_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// Called for the air the volumetric light marches through (volumetricAirMedium in pt/pt_volumetric.glsl), only when
// Volumetric Light is on. Per colour channel, per block:
//   cosTheta   cosine of the angle between the view ray and the sun or moon
//   scattering light scattered towards the eye (phase function included): raise for brighter, thicker-looking haze
//   extinction light lost along the way: raise for denser fog; keep it at least as large as scattering
void hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction) {}

#endif
