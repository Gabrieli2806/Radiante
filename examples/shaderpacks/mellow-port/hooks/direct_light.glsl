#ifndef RADIANTE_HOOK_DIRECT_LIGHT_GLSL
#define RADIANTE_HOOK_DIRECT_LIGHT_GLSL

// Mellow port: sunlight and moonlight in Mellow's colours. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW):
// SUN_DIRECT in get_sun_color, shaders/global/lighting/light_colors.vsh (warm white at noon, orange at sunrise, deeper
// orange at sunset, a cool moon). Only the hue is taken: the tint is scaled to a luminance of 1, so the light keeps
// Radiante's strength, shadows and falloff. Radiante's sun already reddens through the air near the horizon, so the
// tint is blended in by Sun Colour rather than replacing it.
#ifndef MELLOW_SUN_TINT
#    define MELLOW_SUN_TINT 0.7
#endif

#include "mellow/mellow_common.glsl"

void hookDirectLight(SampledSurface surface, vec3 viewDir, vec3 lightDir, float NoL, inout vec3 lightBRDF) {
    vec3 sunDir = normalize(skyUBO.sunDirection);
    MellowTime t = mellowTime(sunDir);
    vec3 hue = mellowSunHue(t, dot(lightDir, sunDir) < 0.0);
    hue /= max(mellowLuminance(hue), 1e-4);
    lightBRDF *= mix(vec3(1.0), hue, float(MELLOW_SUN_TINT));
}

#endif
