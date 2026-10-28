#ifndef RADIANTE_HOOK_WATER_SURFACE_GLSL
#define RADIANTE_HOOK_WATER_SURFACE_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// Called in the default closest-hit shader for every face of water a ray hits, after the water's material and its
// waves are worked out and before hookSurface, which sees the result. For a pack's own water normals, foam or tint.
//   surface    the water as radiante-pt shades it: shadingNormal (bent by the waves), albedoValue and mat.albedo
//              (the biome's water colour), mat.roughness. Setting mat.transmission to 0 makes that point an opaque
//              surface lit like any other (foam, ice), which the camera and the light no longer see through
//   position   the point hit, absolute world position (worldUBO.cameraPos is already added)
//   waveHeight the height of radiante-pt's waves there, in blocks (about -0.35 to 0.35, times hookWaterWaves'
//              strength and size): high on the crests, where foam gathers. 0 where there are no such waves
//              (vanilla or Bedrock water, the sides of water)
// surface.geometricNormal faces the ray: its y is negative where the camera looks up at the surface from under water.
// Time for an animation: worldUBO.gameTime * 1200.0 is the world's clock in seconds.
void hookWaterSurface(inout SampledSurface surface, vec3 position, float waveHeight) {}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_WATER_SURFACE_DEFAULT 1

#endif
