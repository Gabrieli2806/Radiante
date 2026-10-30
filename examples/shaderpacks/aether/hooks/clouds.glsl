#ifndef RADIANTE_HOOK_CLOUDS_GLSL
#define RADIANTE_HOOK_CLOUDS_GLSL

// Aether: soft layered clouds, drifting. Soft cumulus with bright silver linings where the sun shines through their
// thin edges, darker bellies, and the colour of the hour (white by day, gold and rose at golden hour, pink undersides
// lit by the sun just set at blue hour, silver-blue under the moon), over high streaks of cirrus that catch the
// sunset. Drawn after the rain greys the sky and after the sun and moon, so they cover them, and over the stars and
// the milky way. Where they are is aether/aether_cloud_shape.glsl, which hooks/cloud_shadow.glsl reads too, so their
// shadows drift over the ground with them.
//
// Why the pack's own layer and not radiante-pt's volumetric clouds with only a shadow added: a layer looked up from
// the noise image costs a few texture reads where the volumetric clouds march dozens of steps per pixel, its look
// (painted, silver-lined, coloured by the hour like the pack's sky) is the pack's to choose, it has a cheap smooth
// version for the rays that gather light, and the shadow is the very cloud the sky shows. Radiante PT's own clouds
// are off by default in this pack (configs.json); with Aether Clouds off, choose them again in Radiante's settings.
//
// Brightness: lit by the pack's sun and vault colours at Radiante PT's sky brightness after the rain
// (aetherSkyReference), so a sunlit cloud is a few times brighter than the sky round it by day, grey in rain, and
// faint under the moon.
#include "aether/aether_sky.glsl"
#include "aether/aether_cloud_shape.glsl"

struct AetherCloudLight {
    vec3 lightDir;
    vec3 sun;
    vec3 ambient;
};

AetherCloudLight aetherCloudLight(AetherTime t) {
    AetherCloudLight light;
    vec3 sun = radianteSunDirection();
    // The sun lights the clouds until it is well under the horizon (the pink undersides of blue hour); then the moon.
    bool moon = sun.y < -0.10;
    light.lightDir = moon ? radianteMoonDirection() : sun;
    float sky = aetherSkyReference(true);
    float rain = radianteRain();
    float grey = clamp(rain * 0.8 + radianteThunder() * 0.2, 0.0, 1.0);
    float sunGain = moon ? 1.6 * smoothstep(-0.05, 0.15, light.lightDir.y)
                         : 3.2 * t.day + 2.6 * t.golden + 1.4 * t.blue * smoothstep(-0.16, -0.04, sun.y);
    light.sun = aetherSaturate(aetherSunHue(t, moon), 1.0 - grey) * (sky * sunGain * (1.0 - 0.85 * rain));
    // Thunderheads are darker than the grey rain sky round them.
    light.ambient = aetherSaturate(aetherAmbientHue(t), 1.0 - grey) *
                    (sky * mix(0.95, 1.1, rain) * (1.0 - 0.35 * radianteThunder()));
    return light;
}

// The cumulus as light scattered towards the ray (rgb) and the sky let through (a).
vec4 aetherCumulus(vec3 dir, AetherCloudLight light, AetherCloudOffsets offsets, bool seen) {
    vec2 p = aetherLayerPoint(dir, AETHER_CUMULUS_HEIGHT);
    float density = aetherCumulusDensity(p, offsets, seen);
    if (density <= 0.002) { return vec4(0.0, 0.0, 0.0, 1.0); }
    // How much cloud the light crosses to get here: a look a little towards the light (seen), or this density.
    float towardsLight = density;
    if (seen) {
        vec2 lightStep = light.lightDir.xz / max(light.lightDir.y, 0.2) * 140.0;
        towardsLight = aetherCumulusDensity(p + lightStep, offsets, false);
    }
    // Dense middles and the side away from the light darker (bellies), thin parts brighter.
    float selfShadow = exp(-(density * 0.4 + towardsLight * 0.6) * 1.6);
    float powder = 1.0 - exp(-density * 3.0);
    // Forward scattering towards the light, normalized to 1 for an even spread and held under 8.
    float forward = min(aetherPhase(dot(dir, light.lightDir), 0.72) * 4.0 * AETHER_PI, 8.0);
    // The thin parts of a cloud, its rim, glow most when the light is behind it: the silver lining.
    float rim = 4.0 * density * (1.0 - density);
    vec3 colour = light.ambient * (0.72 + 0.28 * (1.0 - density));
    colour += light.sun * (selfShadow * (0.5 + 0.5 * powder) * (0.75 + 0.25 * forward) + rim * forward * 0.22);
    // Far clouds melt into the haze over the horizon, paler and brighter than the vault.
    float far = 1.0 - exp(-length(p) / (AETHER_CUMULUS_HEIGHT * 6.0));
    colour = mix(colour, aetherSaturate(light.ambient, 0.45) * 1.35, far * 0.6);
    float alpha = aetherCumulusAlpha(density) * aetherCumulusHorizon(dir) * (1.0 - far * 0.35);
    return vec4(colour * alpha, 1.0 - alpha);
}

// The cirrus: thin ice high up, bright and taking the sunset's colours, faint by day.
vec4 aetherCirrus(vec3 dir, AetherCloudLight light, AetherCloudOffsets offsets) {
    float amount = clamp(float(AETHER_CIRRUS), 0.0, 1.0);
    if (amount <= 0.0) { return vec4(0.0, 0.0, 0.0, 1.0); }
    vec2 p = aetherLayerPoint(dir, AETHER_CIRRUS_HEIGHT);
    float density = aetherCirrusDensity(p, offsets) * amount;
    if (density <= 0.002) { return vec4(0.0, 0.0, 0.0, 1.0); }
    float forward = min(aetherPhase(dot(dir, light.lightDir), 0.6) * 4.0 * AETHER_PI, 5.0);
    vec3 colour = light.ambient * 0.8 + light.sun * (0.55 + 0.3 * forward);
    float alpha = density * 0.45 * smoothstep(0.02, 0.2, dir.y);
    return vec4(colour * alpha, 1.0 - alpha);
}

vec4 hookCloudLayer(vec3 direction) {
#if AETHER_CLOUDS
    if (direction.y <= 0.0) { return vec4(0.0, 0.0, 0.0, 1.0); }
    AetherTime t = aetherTime();
    AetherCloudLight light = aetherCloudLight(t);
    AetherCloudOffsets offsets = aetherCloudOffsets();
    // Rays that gather light get the cumulus alone, without its ragged detail or the look towards the light: smooth,
    // as the denoiser needs it, and cheap, as many of them reach the sky per pixel.
    bool seen = aetherSeen();
    vec4 cumulus = aetherCumulus(direction, light, offsets, seen);
    vec4 cirrus = seen ? aetherCirrus(direction, light, offsets) : vec4(0.0, 0.0, 0.0, 1.0);
    return vec4(cumulus.rgb + cumulus.a * cirrus.rgb, cumulus.a * cirrus.a);
#else
    return vec4(0.0, 0.0, 0.0, 1.0);
#endif
}

#endif
