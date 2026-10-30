#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// Inkwell: poster colours. Each texel's brightness is snapped to a few levels, like the flat inks of a comic; the
// shading still comes from the path tracer. The texture's own value (albedoValue, also the denoiser's albedo guide)
// and the material's albedo change by the same ratio, so the denoiser puts back the same flat colours.
//
// Only the brightness is snapped, keeping each texel's hue. Snapping each channel on its own was tried in the test
// world first: a green grass texel whose red sat just over a step came out orange, and lawns turned to red and orange
// speckles; leaves, whose texels sit on many such edges, went black and yellow.
#ifndef INKWELL_POSTER_LEVELS
#    define INKWELL_POSTER_LEVELS 6
#endif

void hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce) {
#if INKWELL_POSTER_LEVELS > 1
    // Glowing texels keep their colour: posterized, a lamp's gradient turned into rings.
    if (surface.mat.emission > 0.0) { return; }
    const float levels = float(INKWELL_POSTER_LEVELS);
    vec3 original = clamp(surface.albedoValue.rgb, 0.0, 1.0);
    float luma = dot(original, vec3(0.2126, 0.7152, 0.0722));
    if (luma <= 1e-4) { return; }
    // Snapped in a perceptual space (about sRGB), so the dark tones keep as many steps as the light ones; never
    // below half, so a dark texel does not go black and stop the light bouncing off it.
    float snapped = pow(floor(pow(luma, 1.0 / 2.2) * levels + 0.5) / levels, 2.2);
    float ratio = clamp(max(snapped, luma * 0.5) / luma, 0.0, 1.0 / max(max(original.r, max(original.g, original.b)), 1e-4));
    surface.albedoValue.rgb = original * ratio;
    surface.mat.albedo *= ratio;
#endif
}

#endif
