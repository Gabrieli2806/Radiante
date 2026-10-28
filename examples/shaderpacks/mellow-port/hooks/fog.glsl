#ifndef RADIANTE_HOOK_FOG_GLSL
#define RADIANTE_HOOK_FOG_GLSL

// Mellow port: Mellow's atmospheric fog in Radiante's volumetric light (Volumetric Light on). Mellow's air scatters
// 0.02 and absorbs 0.0025 per unit of density, about 0.2 near the ground at its default Atmospheric Fog Strength
// (0.25), with a forward phase of 0.6. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): get_atm_fog and
// trace_godrays in shaders/global/atmosphere/fog.glsl, xlf_phase in shaders/lib/util.glsl. Radiante traces the
// shadows the fog shows; Mellow traced them in screen space.
#ifndef MELLOW_FOG
#    define MELLOW_FOG 0.25
#endif

float mellowFogPhase(float angle, const float g) {
    float g2 = g * g;
    float denom = (1.0 + g2 - 2.0 * g * angle);
    float result = 1.5 * ((1.0 - g2) / (2.0 + g2)) * ((1.0 + angle * angle) / denom) + g * angle;
    return result / (4.0 * 3.14159265);
}

void hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction) {
    const float FOG_ABSORPTION = 0.0025;
    const float FOG_SCATTERING = 0.02;
    float density = 0.2 * float(MELLOW_FOG) / 0.25;
    float phase = mellowFogPhase(cosTheta, 0.6);
    scattering += vec3(FOG_SCATTERING * density * phase);
    extinction += vec3((FOG_SCATTERING + FOG_ABSORPTION) * density);
}

#endif
