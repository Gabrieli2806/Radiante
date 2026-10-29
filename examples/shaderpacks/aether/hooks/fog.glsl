#ifndef RADIANTE_HOOK_FOG_GLSL
#define RADIANTE_HOOK_FOG_GLSL

// Aether: haze in the air, with Volumetric Light on. A light haze all day that thickens and turns golden when the
// sun is low (more so in the morning), so the sun's rays show through trees and gaps as god rays; a bluer veil at
// blue hour and a little at night. Valley Mist adds mist where the camera is low (under about 110 blocks): a little
// on still mornings, much more in rain, when valleys fill with it; high up on mountains the haze thins.
//
// The hook knows only the angle to the sun, not where in the air it is, so height is the camera's: standing in a
// valley thickens all the haze in view. Forward scattering (Henyey-Greenstein, g 0.68, held under 1 so the air in
// front of the sun does not turn into a white blot) makes the haze glow towards the sun and the shafts stand out;
// the extinction stays above the scattering in every channel, as the hook asks.
#include "aether/aether_common.glsl"

void hookFog(float cosTheta, inout vec3 scattering, inout vec3 extinction) {
    float haze = clamp(float(AETHER_HAZE), 0.0, 3.0);
    float mist = clamp(float(AETHER_VALLEY_MIST), 0.0, 1.0);
    // The overworld's air only: elsewhere the sun's direction (and so the time of day) means nothing.
    if (worldUBO.skyType != 1u || (haze <= 0.0 && mist <= 0.0)) { return; }
    AetherTime t = aetherTime();
    float rain = radianteRain();
    float height = float(worldUBO.cameraPos.y);
    float valley = 1.0 - smoothstep(58.0, 110.0, height);
    float highUp = smoothstep(130.0, 220.0, height);
    float golden = clamp(t.golden * (1.0 + 0.3 * t.dawn), 0.0, 1.3);
    float amount = haze * (0.35 + 1.6 * golden + 0.6 * t.blue + 0.3 * t.night) * (1.0 - 0.5 * highUp);
    amount += mist * valley * (1.2 * t.dawn * (t.golden + t.blue) + 1.8 * rain);
    float density = 0.00035 * amount;
    if (density <= 0.0) { return; }
    // The light the haze scatters: golden with a low sun, cooler at blue hour, grey in rain (each channel at most 1,
    // so the extinction below stays the larger).
    vec3 tint = mix(vec3(1.0), vec3(1.0, 0.80, 0.58), clamp(golden, 0.0, 1.0) * 0.85);
    tint = mix(tint, vec3(0.80, 0.88, 1.0), t.blue * 0.6);
    tint = mix(tint, vec3(0.92), rain * 0.8);
    float phase = mix(1.0 / (4.0 * AETHER_PI), min(aetherPhase(cosTheta, 0.68), 1.0), 0.75);
    scattering += tint * (density * phase);
    extinction += vec3(density * 1.05);
}

#endif
