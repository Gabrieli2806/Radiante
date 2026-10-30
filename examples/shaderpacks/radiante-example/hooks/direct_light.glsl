#ifndef RADIANTE_HOOK_DIRECT_LIGHT_GLSL
#define RADIANTE_HOOK_DIRECT_LIGHT_GLSL

// Radiante Example Pack: the sun behind a leaf or a small plant lights it from the back (light through the leaf).
// The shadow ray starts on the lit side of the face, so this works with opacity micromaps: only this BRDF is new.
void hookDirectLight(SampledSurface surface, vec3 viewDir, vec3 lightDir, float NoL, inout vec3 lightBRDF) {
#if EXAMPLE_FOLIAGE_TRANSLUCENCY != 0
    if (g_exampleFoliage && NoL < 0.0) {
        // Diffuse transmission: the leaf's colour, deepened (light through a leaf comes out greener), times how
        // squarely the sun meets the back.
        vec3 tint = pow(max(surface.albedoValue.rgb, vec3(0.0)), vec3(1.6));
        lightBRDF = tint * (EXAMPLE_FOLIAGE_TRANSMISSION * -NoL / PI);
    }
#endif
}

#endif
