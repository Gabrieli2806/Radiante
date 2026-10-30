#ifndef RADIANTE_HOOK_FOG_GLSL
#define RADIANTE_HOOK_FOG_GLSL

// First Light: the thick, sunlit haze of the 2019 and 2020 RTX showcases, where every window, tree and archway threw
// visible shafts of light. A forward-scattering haze (Henyey-Greenstein, g 0.6) added to Radiante's air, so the shafts
// are brightest looking towards the sun and the shadowed air between them reads clearly. It thickens towards sunrise
// and sunset (FL_DUSK_FOG), when the low sun crosses the most air, and a little in rain. Neutral in colour: the
// sunlight and the sky tint it, as they did in the showcases.
//
// This hook is only called for Radiante's own air (pt/pt_volumetric.glsl, and the froxel fog without Bedrock's fog
// volume: shadeBedrockFogCell does not call it), so the pack turns Bedrock Volumetric Fog off by default in
// configs.json and raises Light Shafts. The player can turn it back on; the haze is then Bedrock's, not this one.
#ifndef FL_GOD_RAYS
#    define FL_GOD_RAYS 0.6
#endif
#ifndef FL_DUSK_FOG
#    define FL_DUSK_FOG 0.8
#endif

#include "first_light/fl_common.glsl"

void hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction) {
    float strength = max(float(FL_GOD_RAYS), 0.0);
    if (strength <= 0.0) { return; }
    vec4 day = radianteDayWeights();
    float dusk = (day.x + day.z) * max(float(FL_DUSK_FOG), 0.0);
    float density = 0.004 * strength * (1.0 + dusk) * (1.0 + 0.5 * radianteRain());
    // Capped and blended with an even share, as radiante-pt does for its own air, so the haze in front of the sun
    // does not turn into a white blot round it.
    const float g = 0.6;
    float hg = (1.0 - g * g) / (12.566371 * pow(max(1.0 + g * g - 2.0 * g * cosTheta, 1e-4), 1.5));
    float phase = mix(0.0795775, min(hg, 2.0), 0.65);
    scattering += vec3(density * phase);
    // At least the scattering, as the hook asks: the phase averages to 1/(4 pi) over the sphere, well under 0.6.
    extinction += vec3(density * 0.6);
}

#endif
