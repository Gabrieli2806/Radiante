#ifndef RADIANTE_HOOK_UNDERWATER_FOG_GLSL
#define RADIANTE_HOOK_UNDERWATER_FOG_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// How far the camera sees while it is under water, and the colour the view fades into (world/world.rgen, once per
// pixel, only with the camera in water). What lies behind the water fades towards the colour over the distance; the
// light of the water round the camera (the sky over it, dimmer deeper down) is applied after this hook.
//   colour     the colour the view fades into, before that light: the biome's water fog (Biome Ambiance on) or a
//              clear sea blue
//   visibility the distance in blocks over which the view fades to about a third (e^-1); raise for clearer water
// The water a ray crosses elsewhere (seen from above, in reflections) is hooks/water.glsl's; a pack's own effect on
// the finished picture under water (distortion, a tint) goes in hooks/underwater.glsl.
void hookUnderwaterFog(inout vec3 colour, inout float visibility) {}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_UNDERWATER_FOG_DEFAULT 1

#endif
