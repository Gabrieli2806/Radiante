#ifndef RADIANTE_HOOK_FOG_GLSL
#define RADIANTE_HOOK_FOG_GLSL

// Neon Dusk: a pink and cyan haze in Radiante's volumetric light. The air scatters pink towards the sun or moon and
// cyan away from it, so looking into the light the shafts and the glow are pink and the rest of the air a cool cyan;
// it is thicker in the dusk and night looks and in rain. The sun's shafts through leaves and windows are its god rays.
//
// This hook is only called for Radiante's own air (pt/pt_volumetric.glsl, and the froxel fog without Bedrock's fog
// volume), so the pack turns Bedrock Volumetric Fog off by default in configs.json and raises Light Shafts; the
// player can turn it back on, and the haze is then gone. In the froxel fog the extinction it adds also makes block
// lights glow in the air (pt/pt_froxel.glsl scatters them by the air's extinction), so the neon lights get halos.
#ifndef NEON_HAZE
#    define NEON_HAZE 0.35
#endif

#include "neon/neon_common.glsl"

void hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction) {
    float haze = max(float(NEON_HAZE), 0.0);
    if (haze <= 0.0) { return; }
    vec3 w = neonLookWeights();
    float density = 0.0035 * haze * (0.6 * w.x + w.y + 1.2 * w.z) * (1.0 + radianteRain());
    // Henyey-Greenstein with g 0.55, capped and blended with an even share as radiante-pt does for its own air, so
    // the haze in front of the sun does not become a white blot round it.
    const float g = 0.55;
    float hg = (1.0 - g * g) / (12.566371 * pow(max(1.0 + g * g - 2.0 * g * cosTheta, 1e-4), 1.5));
    float phase = mix(0.0795775, min(hg, 1.5), 0.6);
    vec3 pink = vec3(1.0, 0.42, 0.8);
    vec3 cyan = vec3(0.35, 0.85, 1.0);
    vec3 tint = mix(cyan, pink, smoothstep(0.1, 0.9, cosTheta * 0.5 + 0.5));
    scattering += tint * density * phase;
    // Grey: the haze tints by what it scatters, not by what it takes away. At least the scattering, as the hook asks.
    extinction += vec3(density);
}

#endif
