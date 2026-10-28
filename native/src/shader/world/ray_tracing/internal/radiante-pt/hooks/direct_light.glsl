#ifndef RADIANTE_HOOK_DIRECT_LIGHT_GLSL
#define RADIANTE_HOOK_DIRECT_LIGHT_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// Called for every sun or moon light sample at a surface, before its shadow ray, once the BRDF is known.
//   surface   the surface being lit (after hookSurface)
//   viewDir   towards the viewer, unit length
//   lightDir  towards the sampled point of the sun or moon, unit length
//   NoL       dot(lightDir, surface.geometricNormal); negative when the light is behind the surface
//   lightBRDF the BRDF times the cosine for this sample: what the light the shadow ray lets through is multiplied
//             by. Zero when the light is behind an opaque surface; leave it zero there to skip the shadow ray, or
//             make it non-zero to light the surface from behind (light through leaves).
void hookDirectLight(SampledSurface surface, vec3 viewDir, vec3 lightDir, float NoL, inout vec3 lightBRDF) {}

// Only in this default file: radiante-pt then keeps its own early exit for light behind a surface, so its shaders are
// the same as before the hook existed. A pack's file must not define it.
#define RADIANTE_HOOK_DIRECT_LIGHT_DEFAULT 1

#endif
