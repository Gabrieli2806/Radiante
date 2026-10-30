#ifndef RADIANTE_HOOK_BLOCK_LIGHT_GLSL
#define RADIANTE_HOOK_BLOCK_LIGHT_GLSL

// Neon Dusk: neon block light. Every block light is pushed towards a neon colour picked by its own: warm lights
// (torches, lanterns, glowstone, redstone, lava) towards hot magenta, cool ones (soul fire, sea lanterns) towards
// cyan, green ones (copper lanterns, glow lichen) towards cyan as well, and the whites (end rods, froglights' paler
// texels) towards an electric violet in between. Then all of it is brightened by NEON_LIGHT_STRENGTH.
//
// The same change for every kind: the light a block casts on the surfaces and the haze, the held light, and the glow
// of the glowing texels themselves (RADIANTE_BLOCK_LIGHT_GLOW), so the flame, the room it lights and the fog round
// it agree, and a glow a bounce ray meets is as bright as the light it stands for. There is no block id (lights are
// cut out of glowing textures), so lava turns magenta with the torches.
#ifndef NEON_LIGHT_COLOUR
#    define NEON_LIGHT_COLOUR 0.75
#endif
#ifndef NEON_LIGHT_STRENGTH
#    define NEON_LIGHT_STRENGTH 1.8
#endif

const vec3 NEON_MAGENTA = vec3(1.0, 0.12, 0.62);
const vec3 NEON_VIOLET = vec3(0.58, 0.22, 1.0);
const vec3 NEON_CYAN = vec3(0.1, 0.85, 1.0);

// The neon colour for a light of this colour, at about its brightness: half way (geometrically) between keeping its
// luminance and keeping its sum of channels. Keeping the luminance alone made a torch's magenta more than three times
// its energy, since magenta has little green; keeping the sum, it looked dimmer than the torch.
vec3 neonLightColour(vec3 light) {
    float brightest = max(light.r, max(light.g, light.b));
    if (brightest <= 1e-6) { return light; }
    vec3 n = light / brightest;
    float warmth = n.r - n.b;
    float greenness = n.g - max(n.r, n.b);
    vec3 target = warmth >= 0.0 ? mix(NEON_VIOLET, NEON_MAGENTA, smoothstep(0.05, 0.45, warmth))
                                : mix(NEON_VIOLET, NEON_CYAN, smoothstep(0.05, 0.45, -warmth));
    target = mix(target, NEON_CYAN, smoothstep(0.1, 0.35, greenness));
    const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    float byLuminance = dot(light, luma) / dot(target, luma);
    float bySum = (light.r + light.g + light.b) / (target.r + target.g + target.b);
    return target * sqrt(byLuminance * bySum);
}

void hookBlockLight(inout vec3 radiance, uint kind) {
    float colour = clamp(float(NEON_LIGHT_COLOUR), 0.0, 1.0);
    if (colour > 0.0) { radiance = mix(radiance, neonLightColour(radiance), colour); }
    radiance *= max(float(NEON_LIGHT_STRENGTH), 0.0);
}

#endif
