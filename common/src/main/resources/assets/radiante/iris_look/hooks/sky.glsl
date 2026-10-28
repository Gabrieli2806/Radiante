#ifndef RADIANTE_HOOK_SKY_GLSL
#define RADIANTE_HOOK_SKY_GLSL

// Radiante look: the sky's colour made richer or paler, at its own brightness, so the light it casts keeps its
// strength. 1 keeps Radiante's sky. Changes live.
#ifndef LOOK_SKY_SATURATION
#    define LOOK_SKY_SATURATION 1.0
#endif

void hookSky(inout vec3 radiance, vec3 direction) {
    const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    float y = dot(radiance, luma);
    vec3 tinted = max(mix(vec3(y), radiance, float(LOOK_SKY_SATURATION)), vec3(0.0));
    radiance = tinted * (y / max(dot(tinted, luma), 1e-6));
}

#endif
