#ifndef RADIANTE_HOOK_RAY_ORIGINS_GLSL
#define RADIANTE_HOOK_RAY_ORIGINS_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signatures
// stay as they are within radiante-pt@1.
//
// Where a hit's shadow rays and reflection rays start, apart from where it is shaded: Pixelated Lighting starts them
// from the centre of the texture texel (hookTexelCentre), so shadow edges and reflections come out in the texture's
// pixels while the light itself stays smooth. radiante-pt's own file only defines RADIANTE_HOOK_RAY_ORIGINS_DEFAULT,
// and with it everything below is compiled out. A pack's file leaves that define out and gives
//   vec3 hookShadowOffset()      how far to move the shadow rays of the hit being shaded
//   vec3 hookReflectionOffset()  how far to move its reflection rays
// both camera-relative offsets from the shaded point (surface.worldPos as hookSurface leaves it), zero to keep it;
// hookSurface, which runs first, can work them out and keep them in globals this file declares.
//
// With an offset, radiante-pt:
// - starts every shadow ray of the hit (the sun or moon, the block light, the held lights: pt_deferred_shadow.glsl)
//   that far away, still aimed at the same point of a block or held light (the sun and moon keep their direction).
//   Only the visibility moves: the light, its angle and its falloff stay the shaded point's.
// - starts the reflection the shading picks (the specular lobe, water's reflection, a mirror looked into) from the
//   moved point, turned by as much as the view of the moved point reflects differently from the view of the hit, so
//   every pixel that gets the same offset reflects the same way.
// - for the surface the camera sees, hands the moved point to the ray generation in a fourth layer of the
//   material_state image (the pack gives it "depth": 4 in its configs.json), which traces the reflection of water
//   and glass from there, seen from there. Refraction and diffuse bounces keep the shaded point.
// Included before the shading (pt/pt_deferred_shadow.glsl) and the ray generation (pt/pt_primary.glsl), so a
// pack's file must not need more than the world uniform and its own globals.
#define RADIANTE_HOOK_RAY_ORIGINS_DEFAULT

#endif
