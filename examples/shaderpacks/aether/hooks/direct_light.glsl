#ifndef RADIANTE_HOOK_DIRECT_LIGHT_GLSL
#define RADIANTE_HOOK_DIRECT_LIGHT_GLSL

// Aether: a warm sun and a cool moon, and sunlight through leaves.
//
// Sun Warmth tints the sun's light a touch warm by day and warmer at golden hour (on top of the reddening Radiante PT
// gives it through the air), and the moon's a cool silver-blue; only the hue, the strength stays Radiante's. With the
// sky's light in the shade staying close to Radiante's own blue, lit sides warm and shaded sides cool, the balance
// a cinematographer lights for; the grade's teal shadows finish it.
//
// Light Through Leaves: the sun behind a leaf or a small plant lights it from the back (g_aetherFoliage, set by
// hooks/surface.glsl), in the leaf's colour deepened as light through a leaf is, and brightest looking towards the
// sun through it, so backlit canopies glow. The shadow ray starts on the lit side, so this keeps opacity micromaps.
#include "aether/aether_common.glsl"

void hookDirectLight(SampledSurface surface, vec3 viewDir, vec3 lightDir, float NoL, inout vec3 lightBRDF) {
    float leaf = clamp(float(AETHER_LEAF_LIGHT), 0.0, 3.0);
    if (g_aetherFoliage && NoL < 0.0 && leaf > 0.0) {
        vec3 tint = pow(max(surface.albedoValue.rgb, vec3(0.0)), vec3(1.5));
        float through = pow(max(dot(-viewDir, lightDir), 0.0), 6.0);
        lightBRDF = tint * (leaf * -NoL * (0.45 + 1.1 * through) / AETHER_PI);
    }

    float warmth = clamp(float(AETHER_SUN_WARMTH), 0.0, 1.0);
    // The overworld's sun and moon only, not the End's flash.
    if (warmth > 0.0 && worldUBO.skyType == 1u) {
        bool moon = dot(lightDir, radianteSunDirection()) < 0.0;
        vec3 hue;
        if (moon) {
            hue = vec3(0.86, 0.94, 1.12);
        } else {
            AetherTime t = aetherTime();
            hue = t.day * vec3(1.05, 1.00, 0.92) + t.golden * vec3(1.14, 0.96, 0.78) + (t.blue + t.night) * vec3(1.0);
        }
        lightBRDF *= mix(vec3(1.0), hue / aetherLuma(hue), warmth);
    }
}

#endif
