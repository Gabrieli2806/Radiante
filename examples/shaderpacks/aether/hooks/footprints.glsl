#ifndef RADIANTE_HOOK_FOOTPRINTS_GLSL
#define RADIANTE_HOOK_FOOTPRINTS_GLSL

#include "common/pack_world.glsl"
#include "aether/aether_settings.glsl"

// Aether: deeper footprints. AETHER_FOOTPRINTS (live, 0 to 2) scales how pressed each print looks, in sand and mud
// and in snow alike, and their rims with them: 1 is Radiante PT's, 0 hides them. While the air is still wet from rain
// (radianteWetness, which dries over a minute or so) a print in sand or mud holds water, darker and glossy, as a step
// in a wet beach does.
void hookFootprints(inout float soft, inout float packed, inout float slush, inout float snow, inout float wet,
                    inout float rim, vec3 position, uint surfaceFlags) {
    float depth = clamp(float(AETHER_FOOTPRINTS), 0.0, 2.0);
    soft *= depth;
    packed *= depth;
    slush *= depth;
    rim *= depth;
    wet = max(wet, min(soft, 1.0) * radianteWetness() * 0.8);
}

#endif
