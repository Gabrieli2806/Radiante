#ifndef RADIANTE_HOOK_FOG_GLSL
#define RADIANTE_HOOK_FOG_GLSL

// Mellow port: Mellow's atmospheric fog in Radiante's volumetric light (Volumetric Light on). Ported from Mellow by
// TheCMK (MIT, LICENSE-MELLOW): get_atm_fog in shaders/global/atmosphere/fog.glsl, the fogAmount uniform in
// shaders/shaders.properties and xlf_phase in shaders/lib/util.glsl. Mellow's air scatters 0.02 and absorbs 0.0025
// per unit of density; the density is 0.27 near the ground, times fogAmount (1, plus 1 at sunrise and at night, plus
// the rain), times Atmospheric Fog Strength (0.25), with a forward phase of 0.6. Radiante traces the shadows the fog
// shows; Mellow traced them in screen space. Mellow's fog fades out from 60 to 100 blocks up and stops at 128 blocks
// away; Radiante's volumetric light has its own reach, so neither is ported.
#ifndef MELLOW_FOG
#    define MELLOW_FOG 0.25
#endif

#include "mellow/mellow_common.glsl"

void hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction) {
    const float FOG_ABSORPTION = 0.0025;
    const float FOG_SCATTERING = 0.02;
    MellowTime t = mellowTime(normalize(skyUBO.sunDirection));
    float fogAmount = 1.0 + t.sunrise + t.night + skyUBO.rainGradient;
    float density = 0.27 * fogAmount * float(MELLOW_FOG);
    float phase = mellowPhase(cosTheta, 0.6);
    scattering += vec3(FOG_SCATTERING * density * phase);
    extinction += vec3((FOG_SCATTERING + FOG_ABSORPTION) * density);
}

#endif
