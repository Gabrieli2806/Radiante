#ifndef RADIANTE_HOOK_DIMENSION_SKY_GLSL
#define RADIANTE_HOOK_DIMENSION_SKY_GLSL

// Radiante Example Pack: a thicker, warmer Nether haze. Nether Haze multiplies how fast things fade into the
// Nether's fog (1 is radiante-pt's) and pulls its colour a little towards orange at the same brightness. The End and
// the sky are left as radiante-pt draws them.
#ifndef EXAMPLE_NETHER_HAZE
#    define EXAMPLE_NETHER_HAZE 1.5
#endif

void hookNetherSky(inout vec3 radiance, vec3 direction) {}

void hookEndSky(inout vec3 radiance, vec3 direction, bool seen) {}

void hookNetherFog(inout vec3 colour, inout float density) {
    float haze = float(EXAMPLE_NETHER_HAZE);
    density *= haze;
    const vec3 luminance = vec3(0.2126, 0.7152, 0.0722);
    vec3 warm = vec3(1.0, 0.45, 0.2) * (dot(colour, luminance) / dot(vec3(1.0, 0.45, 0.2), luminance));
    colour = mix(colour, warm, clamp((haze - 1.0) * 0.5, 0.0, 0.5));
}

void hookEndFog(inout vec3 colour, inout float density) {}

#endif
