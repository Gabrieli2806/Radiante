#ifndef RADIANTE_HOOK_DIRECT_LIGHT_GLSL
#define RADIANTE_HOOK_DIRECT_LIGHT_GLSL

// Inkwell: cel-shaded sunlight. The sun's light on a surface comes in INKWELL_BANDS flat steps instead of a smooth
// falloff, with a hard-edged highlight on smooth surfaces. Shadows, bounce light and block lights stay path traced,
// so the shadow side is still lit by the sky and the room around it. INKWELL_CEL blends towards radiante-pt's own
// shading (0) and changes live.
#ifndef INKWELL_BANDS
#    define INKWELL_BANDS 3
#endif
#ifndef INKWELL_CEL
#    define INKWELL_CEL 1.0
#endif

void hookDirectLight(SampledSurface surface, vec3 viewDir, vec3 lightDir, float NoL, inout vec3 lightBRDF) {
    if (NoL <= 0.0 || surface.mat.transmission > 0.0) { return; }
    const float bands = float(INKWELL_BANDS);
    float nl = clamp(dot(lightDir, surface.shadingNormal), 0.0, 1.0);
    // Rounded up: any lit part gets at least the first step, so the terminator is a clean line, not a dark rim.
    float band = clamp(ceil(nl * bands - 0.15) / bands, 0.0, 1.0);
    vec3 cel = surface.mat.albedo * (1.0 - surface.mat.metallic) * (band / PI);
    // The highlight: a disc whose size follows the roughness, in the specular colour.
    vec3 halfway = normalize(lightDir + viewDir);
    float smoothness = 1.0 - clamp(surface.mat.roughness, 0.0, 1.0);
    float highlight = step(1.0 - 0.04 * smoothness * smoothness - 0.002, dot(surface.shadingNormal, halfway));
    vec3 specular = mix(surface.mat.f0, surface.mat.albedo, surface.mat.metallic);
    cel += specular * (highlight * smoothness * 4.0 * band);
    lightBRDF = mix(lightBRDF, cel, clamp(float(INKWELL_CEL), 0.0, 1.0));
}

#endif
