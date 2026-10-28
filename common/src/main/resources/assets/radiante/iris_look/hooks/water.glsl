#ifndef RADIANTE_HOOK_WATER_GLSL
#define RADIANTE_HOOK_WATER_GLSL

// Radiante look: how quickly water swallows the view into it. 1 keeps Radiante's water; higher is murkier, lower
// clearer. The converted pack's water fog sets it. Not live: radiante-pt
// already uses nearly all 64 live setting slots.
#ifndef LOOK_WATER_ABSORPTION
#    define LOOK_WATER_ABSORPTION 1.0
#endif

void hookWaterAbsorption(inout vec3 extinction) {
    extinction *= max(float(LOOK_WATER_ABSORPTION), 0.0);
}

#endif
