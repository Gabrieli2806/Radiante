#ifndef RADIANTE_HOOK_DIRECT_LIGHT_GLSL
#define RADIANTE_HOOK_DIRECT_LIGHT_GLSL

// Aquarela: soft light. A watercolour models form with few, close values, so the sun wraps further round matte
// surfaces: the cosine of the light's angle is replaced by a wrapped one, (cos + w) / (1 + w), on faces the sun
// reaches. Faces it grazes come out lighter, faces it meets head on stay as they were, and the step between a block's
// lit sides shrinks. Shadows are still traced (light behind a face is left at zero, so no shadow ray is added), and
// glossy and metal surfaces keep their highlights, which the wrap would only blow up.
// AQUARELA_SOFT_LIGHT is live ("runtime": true), so it is tested with if.
#ifndef AQUARELA_SOFT_LIGHT
#    define AQUARELA_SOFT_LIGHT 0.4
#endif

void hookDirectLight(SampledSurface surface, vec3 viewDir, vec3 lightDir, float NoL, inout vec3 lightBRDF) {
    float soft = clamp(float(AQUARELA_SOFT_LIGHT), 0.0, 1.0);
    if (soft <= 0.0 || NoL <= 0.0 || surface.mat.transmission > 0.0) { return; }
    float cosine = dot(lightDir, surface.shadingNormal);
    if (cosine <= 1e-3) { return; }
    float wrap = 0.6 * soft;
    float wrapped = (cosine + wrap) / (1.0 + wrap);
    float matte = (1.0 - clamp(surface.mat.metallic, 0.0, 1.0)) * smoothstep(0.25, 0.6, surface.mat.roughness);
    lightBRDF *= mix(1.0, min(wrapped / cosine, 4.0), matte);
}

#endif
