#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// Called once per hit the default closest-hit shader shades, after the material is sampled (texture, LabPBR or
// Bedrock maps, rain, lava) and before anything reads it: the denoiser's guides, the mirror test, the lights.
//   surface      what the hit shows; change albedoValue, mat (roughness, metallic, emission...), shadingNormal...
//   surfaceFlags the hit's packed vertex data: isLeavesSurface(surfaceFlags), isPlantSurface, isWaterSurface,
//                isFrostSurface... (util/vertex.glsl)
//   (surface.mat.subSurface above zero sets how much sun the surface lets through from behind, in place of
//   radiante-pt's own share for leaves, plants, ice and snow; see translucentBackLight in pt/pt_direct.glsl.)
//   bounce       0 for what the camera sees first, then 1, 2... along the path
// Globals declared here are visible to hooks/direct_light.glsl, which is included after this file.
void hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce) {}

#endif
