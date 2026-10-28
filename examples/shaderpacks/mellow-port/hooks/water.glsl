#ifndef RADIANTE_HOOK_WATER_GLSL
#define RADIANTE_HOOK_WATER_GLSL

// Mellow port: Mellow's green-teal water. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): the water colour
// f_WATER_RED/GREEN/BLUE (0, 0.35, 0.25) of the default scheme in shaders/lib/colors.glsl, which Mellow mixes over
// what is under the water and uses as the underwater fog. Radiante's water is a medium that absorbs and scatters, so
// the colour becomes the shape of its absorption: each channel is absorbed by -log of its share of Mellow's colour
// (red the most, then blue, green the least), and the total stays Radiante's, so the water is as clear as before and
// only its tint changes. The scattered light takes the same tint, since radiante-pt scatters a share of the
// extinction.
#ifndef MELLOW_WATER
#    define MELLOW_WATER 0.75
#endif

void hookWaterAbsorption(inout vec3 extinction) {
    vec3 water = pow(vec3(0.0, 0.35, 0.25), vec3(2.2));
    vec3 share = max(water / max(max(water.r, water.g), water.b), vec3(0.04));
    vec3 shape = -log(share) + 0.15;
    float total = extinction.r + extinction.g + extinction.b;
    vec3 mellow = shape * (total / (shape.r + shape.g + shape.b));
    extinction = mix(extinction, mellow, float(MELLOW_WATER));
}

#endif
