#ifndef RADIANTE_HOOK_SHADING_POINT_GLSL
#define RADIANTE_HOOK_SHADING_POINT_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// For a pack whose hookSurface moves the point a surface is shaded at (surface.worldPos), as Pixelated Lighting moves
// it to the centre of the texture texel. Included before the shading and the ray generation; radiante-pt's own file
// only defines RADIANTE_HOOK_SHADING_POINT_DEFAULT, and with it everything below is compiled out. A pack's file leaves
// that define out and gives
//   uint hookSampleKey(MainRay ray, uint pixelKey)
// which picks the low-discrepancy samples of a hit (the BSDF's direction and lobe, the sun or moon sample,
// pt/pt_sampling.glsl) in place of the pixel: pixelKey is the pixel's own key, return it to keep radiante-pt's
// sampling. Pixels given the same key at the same bounce take the same samples. (The rest of the path draws from
// mainRay.seed, which hookSurface may set as well.)
// Without the default, the shading also hands back where hookSurface left surface.worldPos for the surface the camera
// sees, in a fourth layer of the material_state image (the pack gives it "depth": 4 in its configs.json), and the
// ray generation starts the reflection of water and glass the camera sees from there rather than from where the
// camera's ray met the face. A smooth metal looked into (the mirror path of pt_shade_default.glsl) is reflected from
// there too. The refraction keeps the camera ray's point, so what is seen through the surface is not moved.
#define RADIANTE_HOOK_SHADING_POINT_DEFAULT

#endif
