#ifndef RADIANTE_HOOK_WATER_WAVES_GLSL
#define RADIANTE_HOOK_WATER_WAVES_GLSL

// Aether: calmer, glassier water that roughens in storms. Calm Water lowers Radiante PT's waves to half their
// steepness, slows them by a quarter and makes them two fifths longer, so the surface reads as a slow glassy swell
// that mirrors the sky; rain and thunder bring the chop back. The caustics under the water follow the same waves.
// Included by world/shadow.rahit too, where the world uniform has another name (radiante-pt defines worldUBO to it).
#include "aether/aether_common.glsl"

void hookWaterWaves(inout float strength, inout float speed, inout float size) {
    float calm = clamp(float(AETHER_WATER_CALM), 0.0, 1.0);
    float storm = clamp(radianteRain() * 0.6 + radianteThunder() * 0.6, 0.0, 1.0);
    strength *= mix(1.0, 0.5, calm) * (1.0 + 0.8 * storm);
    speed *= mix(1.0, 0.75, calm) * (1.0 + 0.4 * storm);
    size *= mix(1.0, 1.4, calm);
}

#endif
