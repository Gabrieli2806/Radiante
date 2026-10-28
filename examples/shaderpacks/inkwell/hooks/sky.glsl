#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Inkwell: a flat two-tone sky, cream at the horizon and a printer's blue above, in two soft bands. It keeps the
// sky's own brightness (day, dusk, night) and only changes its colour, so the light the sky casts keeps its strength.
#ifndef INKWELL_FLAT_SKY
#    define INKWELL_FLAT_SKY 1
#endif

void hookSky(inout vec3 radiance, vec3 direction) {
#if INKWELL_FLAT_SKY != 0
    const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    const vec3 horizon = vec3(1.0, 0.86, 0.66);
    const vec3 zenith = vec3(0.20, 0.45, 0.95);
    float height = clamp(direction.y, 0.0, 1.0);
    float split = smoothstep(0.08, 0.12, height) * 0.65 + smoothstep(0.35, 0.45, height) * 0.35;
    vec3 tone = mix(horizon, zenith, split);
    float brightness = dot(radiance, luma);
    // Dusk keeps a little of its own colour: an orange evening read as plain blue otherwise.
    vec3 own = radiance / max(brightness, 1e-6);
    tone = mix(tone, own, 0.25);
    radiance = tone * (brightness / max(dot(tone, luma), 1e-6));
#endif
}

#endif
