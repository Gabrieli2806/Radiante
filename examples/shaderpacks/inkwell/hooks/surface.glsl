#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// Inkwell: poster colours. Each texel's colour is snapped to a few levels per channel, like the flat inks of a comic;
// the shading still comes from the path tracer. The texture's own value (albedoValue, also the denoiser's albedo
// guide) and the material's albedo change by the same ratio, so the denoiser puts back the same flat colours.
#ifndef INKWELL_POSTER_LEVELS
#    define INKWELL_POSTER_LEVELS 6
#endif

void hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce) {
#if INKWELL_POSTER_LEVELS > 1
    // Glowing texels keep their colour: posterized, a lamp's gradient turned into rings.
    if (surface.mat.emission > 0.0) { return; }
    const float levels = float(INKWELL_POSTER_LEVELS);
    vec3 original = clamp(surface.albedoValue.rgb, 0.0, 1.0);
    // Snapped in a perceptual space (about sRGB), so the dark tones keep as many steps as the light ones.
    vec3 snapped = pow(floor(pow(original, vec3(1.0 / 2.2)) * levels + 0.5) / levels, vec3(2.2));
    // Never to black: a dark texel snapped to zero would stop light bouncing off it.
    snapped = max(snapped, original * 0.5);
    vec3 ratio = snapped / max(original, vec3(1e-4));
    surface.albedoValue.rgb = snapped;
    surface.mat.albedo *= mix(vec3(1.0), ratio, step(vec3(1e-4), original));
#endif
}

#endif
