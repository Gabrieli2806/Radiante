#ifndef RADIANTE_HOOK_UNDERWATER_FOG_GLSL
#define RADIANTE_HOOK_UNDERWATER_FOG_GLSL

// Mellow port: Mellow's colour under water. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): tint_underwater in
// shaders/lib/tonemap.glsl, which tints the fog the camera sees under water with the water colour f_WATER_RED/GREEN/
// BLUE (0, 0.35, 0.25), mixed half with the biome's (f_BIOME_WATER_CONTRIBUTION 0.5) at the water colour's
// brightness (mix_preserve_c1lum). Here the biome's is Radiante's own colour for the view under water, and the result
// keeps its brightness, so only the hue becomes Mellow's green-teal; how far one sees stays Radiante's (Mellow's
// UNDERWATER_FOG_STRENGTH 1). Water Colour sets how much, as it does for the water's absorption (hooks/water.glsl).
#ifndef MELLOW_WATER
#    define MELLOW_WATER 0.75
#endif

void hookUnderwaterFog(inout vec3 colour, inout float visibility) {
    const vec3 luminance = vec3(0.2126, 0.7152, 0.0722);
    float bright = dot(colour, luminance);
    vec3 water = pow(vec3(0.0, 0.35, 0.25), vec3(2.2));
    vec3 mixed = mix(water, colour, 0.5);
    vec3 mellow = mixed * (bright / max(dot(mixed, luminance), 1e-6));
    colour = mix(colour, mellow, float(MELLOW_WATER));
}

#endif
