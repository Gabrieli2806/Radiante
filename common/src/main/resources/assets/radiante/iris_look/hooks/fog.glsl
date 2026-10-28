#ifndef RADIANTE_HOOK_FOG_GLSL
#define RADIANTE_HOOK_FOG_GLSL

// Radiante look: the strength of the haze the volumetric light marches through (only with Volumetric Light on). 1
// keeps Radiante's own; the converted pack's fog strength and distance set it. Changes live.
#ifndef LOOK_FOG
#    define LOOK_FOG 1.0
#endif

void hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction) {
    float strength = max(float(LOOK_FOG), 0.0);
    scattering *= strength;
    extinction *= strength;
}

#endif
