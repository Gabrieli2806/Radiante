#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// Aether: wet ground that lasts after the rain, with puddles, and a note for hooks/direct_light.glsl of whether the
// hit is a leaf or a small plant (light through leaves).
//
// Radiante PT already soaks the ground while it rains (common/rain_wetness.glsl): the top of every column open to
// the sky, darker and glossier, drying off towards walls and overhangs; its wetness follows the rain within seconds.
// The pack keeps that ground wet as long as the air stays wet (radianteWetness, which lingers) and for a few minutes
// after the rain stops (radianteRainStoppedFor), drying it slowly, and lays puddles on it: mirror-smooth, flat, dark
// patches from a low-frequency pattern in the world (the noise image at 64 and 256 blocks), which spread as the
// ground gets wetter. Where the ground is open to the sky is radiante-pt's own answer (vptGroundWetness divided by
// its wetness), so puddles never form under a roof. Leaves and plants get the sheen, never puddles. Every bounce
// sees the same smooth pattern, so nothing here is noise for the denoiser.
//
// Wet Surfaces sets how wet the lingering ground is, Puddles how far they spread. The pack's debug view
// (/radiante debug view aether_wetness) paints the wet ground blue and the puddles white.
#include "aether/aether_noise.glsl"

bool g_aetherFoliage = false;

// How wet radiante-pt's ground is here (0 to 1, its own wetness with the column test and the drying edges), and how
// open to the rain it is (0 to 1, the same without the wetness).
void aetherRadianteWetness(SampledSurface surface, out float own, out float open) {
    own = 0.0;
    open = 0.0;
    float rain = skyUBO.rainWetness.x;
    // Radiante PT keeps the image of which columns are open only while its own wetness lasts (some four minutes
    // after the rain); without it, nothing is wet.
    if (rain <= 1e-4) { return; }
    own = vptGroundWetness(surface.worldPos, surface.geometricNormal);
    open = clamp(own / rain, 0.0, 1.0);
}

void aetherWetGround(inout SampledSurface surface, uint surfaceFlags, uint bounce) {
    float wetSetting = clamp(float(AETHER_WETNESS), 0.0, 1.0);
    float puddleSetting = clamp(float(AETHER_PUDDLES), 0.0, 1.0);
    if (wetSetting <= 0.0 && puddleSetting <= 0.0 && AETHER_DEBUG_WETNESS == 0) { return; }
    if (surface.geometricNormal.y < 0.5) { return; }
    if (isWaterSurface(surfaceFlags) || isLavaSurface(surfaceFlags) || isGlassSurface(surfaceFlags) ||
        isPaneSurface(surfaceFlags) || isHeldSurface(surfaceFlags) || isRainSurface(surfaceFlags)) {
        return;
    }
    if (surface.mat.transmission > 0.0 || surface.mat.emission > 0.0) { return; }
    float own;
    float open;
    aetherRadianteWetness(surface, own, open);
    if (open <= 0.0) { return; }

    // The ground's wetness: the air's, and after a shower that wetted it, a slow drying over three and a half
    // minutes (radianteRainStoppedFor is 0 while it rains, and huge before any rain).
    float stopped = radianteRainStoppedFor();
    float lingering = stopped > 0.0 && stopped < 1e5 ? 1.0 - smoothstep(45.0, 210.0, stopped) : 0.0;
    float wet = max(radianteWetness(), 0.85 * lingering) * open;

    // Puddles: flat, smooth patches on level ground, from the noise image at 64 and 256 blocks, filling as the
    // ground gets wetter. The camera's position is folded into the image's period in double precision.
    float puddle = 0.0;
    if (!g_aetherFoliage) {
        vec2 xz = surface.worldPos.xz + vec2(mod(worldUBO.cameraPos.xz, 1024.0LF));
        float shape = aetherNoiseAt(xz / 64.0).g * 0.6 + aetherNoiseAt(xz / 256.0).r * 0.4;
        float fill = clamp(wet * puddleSetting * 1.25, 0.0, 1.0);
        float threshold = mix(0.92, 0.50, fill);
        puddle = smoothstep(threshold, threshold + 0.06, shape) * smoothstep(0.9, 0.98, surface.geometricNormal.y) *
                 step(0.02, fill);
    }

    // What radiante-pt did already while it rains (own) is not done twice: the lingering wetness only tops it up.
    float extra = max(wet * wetSetting - own, 0.0);
    float absorbency = (1.0 - surface.mat.metallic) * clamp(surface.mat.roughness * 2.0, 0.0, 1.0);
    float darken = 1.0 - 0.30 * extra * absorbency - 0.35 * puddle * absorbency;
    surface.mat.albedo *= darken;
    surface.albedoValue.rgb *= darken;
    float gloss = max(extra, puddle);
    surface.mat.roughness = mix(surface.mat.roughness, min(surface.mat.roughness, 0.10), extra);
    surface.mat.roughness = mix(surface.mat.roughness, 0.035, puddle);
    surface.mat.f0 = mix(surface.mat.f0, max(surface.mat.f0, vec3(0.02)), gloss);
    // Water fills the texture's bumps: the normal map flattens, fully in a puddle.
    surface.shadingNormal = normalize(mix(surface.shadingNormal, surface.geometricNormal, max(extra * 0.6, puddle)));

    if (AETHER_DEBUG_WETNESS != 0 && bounce == 0u) {
        vec3 paint = mix(vec3(0.05, 0.25, 0.9) * max(wet, 0.1), vec3(1.0), puddle);
        surface.mat.albedo = paint;
        surface.albedoValue.rgb = paint;
    }
}

void hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce) {
    g_aetherFoliage = isLeavesSurface(surfaceFlags) || isPlantSurface(surfaceFlags);
    aetherWetGround(surface, surfaceFlags, bounce);
}

#endif
