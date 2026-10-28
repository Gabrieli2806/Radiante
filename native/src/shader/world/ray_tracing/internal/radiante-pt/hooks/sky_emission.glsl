#ifndef RADIANTE_HOOK_SKY_EMISSION_GLSL
#define RADIANTE_HOOK_SKY_EMISSION_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// Light that glows in a pack's overworld sky (an aurora, a rainbow, airglow), for every ray that reaches the sky,
// so what it returns is seen and also lights the world, as a real aurora tints the snow under it. Called in the
// miss shader after hookStars and before hookCloudLayer, so a pack's clouds cover it; a vanilla cloud the ray crossed
// dims it as it dims the sun and moon discs.
//   direction world-space ray direction, unit length (y up)
//   seen      true for the sky the camera sees (its own ray, also through glass or water); false for a ray that
//             bounced off the world. Those reach the sky many times per pixel in every direction: give them a cheap,
//             smooth version (the feature's average, no fine detail, no per-frame dither), or the light it casts is
//             noise the denoiser and SHaRC have to smear out.
// Returns the radiance to add to the sky along direction.
vec3 hookSkyEmission(vec3 direction, bool seen) {
    return vec3(0.0);
}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_SKY_EMISSION_DEFAULT 1

#endif
