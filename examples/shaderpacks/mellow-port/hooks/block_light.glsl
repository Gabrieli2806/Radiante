#ifndef RADIANTE_HOOK_BLOCK_LIGHT_GLSL
#define RADIANTE_HOOK_BLOCK_LIGHT_GLSL

// Mellow port: Mellow's torch light colour. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): TorchColor in
// shaders/global/lighting/lighting.glsl, the default scheme's f_LM_RED/GREEN/BLUE (1.0, 0.75, 0.5) in
// shaders/lib/colors.glsl, a warm orange. Mellow lights everything a block light reaches with that one colour (its
// coloured lights, COLORED_LIGHTS, are off by default); Radiante's lights keep the colour of what glows. Torch Colour
// blends the light cast towards Mellow's at the light's own brightness: 1 is Mellow's (soul fire and redstone light
// turn warm as well), 0 keeps Radiante's. The glowing blocks themselves keep their texture's colour, as in Mellow.
#ifndef MELLOW_TORCH_COLOUR
#    define MELLOW_TORCH_COLOUR 0.5
#endif

void hookBlockLight(inout vec3 radiance, uint kind) {
    if (kind == RADIANTE_BLOCK_LIGHT_GLOW) return;
    vec3 torch = pow(vec3(1.0, 0.75, 0.5), vec3(2.2));
    const vec3 luminance = vec3(0.2126, 0.7152, 0.0722);
    vec3 mellow = torch * (dot(radiance, luminance) / dot(torch, luminance));
    radiance = mix(radiance, mellow, float(MELLOW_TORCH_COLOUR));
}

#endif
