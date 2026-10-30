#ifndef RADIANTE_HOOK_STARS_GLSL
#define RADIANTE_HOOK_STARS_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// A pack's night sky (stars), called in the overworld miss shader only for the sky the camera sees (also through
// glass or water), after rain has greyed the sky and the sun, moon and halos are added, and before
// hookCloudLayer, so a pack's clouds cover it. Not called under water. What should also light the world (an aurora)
// goes in hooks/sky_emission.glsl instead.
//   radiance  the sky's radiance along direction; add the pack's stars to it
//   direction world-space ray direction, unit length (y up)
// Returns how much of radiante-pt's own stars (vanilla's, drawn after the frame) to keep: 1 keeps them, 0 hides them
// where this ray looks, for a pack that draws its own.
float hookStars(inout vec3 radiance, vec3 direction) {
    return 1.0;
}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_STARS_DEFAULT 1

#endif
