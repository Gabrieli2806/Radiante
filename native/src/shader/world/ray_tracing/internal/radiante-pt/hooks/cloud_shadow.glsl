#ifndef RADIANTE_HOOK_CLOUD_SHADOW_GLSL
#define RADIANTE_HOOK_CLOUD_SHADOW_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// The shadow of a pack's clouds (hooks/clouds.glsl) on the world. Called for every sun or moon light sample at a
// surface, after hookDirectLight; the light that sample brings is multiplied by what this returns. The rest of the
// shadow (blocks, entities, radiante-pt's own clouds) is still traced.
//   worldPos  the surface's position, camera-relative (worldUBO.cameraPos adds the camera for an absolute one)
//   lightDir  towards the sampled point of the sun or moon, unit length
// Returns how much of the light the clouds let through, 0 to 1.
float hookCloudShadow(vec3 worldPos, vec3 lightDir) {
    return 1.0;
}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_CLOUD_SHADOW_DEFAULT 1

#endif
