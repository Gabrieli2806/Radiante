#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

// Miss shader for shadow rays: nothing blocked the ray, so it delivers light. Which light depends on the query:
// glow-outline and block-light queries only need "unblocked", celestial queries get the sun or moon, attenuated by
// the atmosphere (transmittance LUT, or Bedrock's own table) and by biome fog, and faded near the horizon.

#include "util/ray.glsl"
#include "util/util.glsl"
#include "common/shared.hpp"

layout(set = 5, binding = 0) uniform sampler2D transLUT;

layout(set = 2, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 2, binding = 2) uniform SkyUniform {
    SkyUBO skyUBO;
};

layout(location = 1) rayPayloadInEXT ShadowRay shadowRay;

#include "common/celestial.glsl"
#include "common/bedrock_atmosphere.glsl"
#include "common/biome_fog_light.glsl"

// Atmosphere transmittance from radius r at sun cosine mu, in the LUT's parameterisation (see trans_lut.frag).
vec3 sampleTransmittance(float r, float mu) {
    vec2 lutCoord = vec2(clamp(mu * 0.5 + 0.5, 0.0, 1.0),
                         clamp((r - VPT_ATMOSPHERE_RG) / (VPT_ATMOSPHERE_RT - VPT_ATMOSPHERE_RG), 0.0, 1.0));
    return sampleTexture(transLUT, lutCoord, false).rgb;
}

// Light arriving at the ray origin from the sun (sun above the horizon) or the moon.
vec3 celestialRadiance(vec3 toSun) {
    if (!(toSun.y > 0)) {
        return VPT_MOON_RADIANCE * worldUBO.moonBrightness * celestialMoonLitFraction();
    }
    // The planet's centre sits RG below the world origin.
    vec3 fromCentre = gl_WorldRayOriginEXT - vec3(0.0, -VPT_ATMOSPHERE_RG, 0.0);
    float radius = length(fromCentre);
    float sunCos = clamp(dot(fromCentre / max(radius, 1e-6), toSun), -1.0, 1.0);
    radius = clamp(radius, VPT_ATMOSPHERE_RG, VPT_ATMOSPHERE_RT);
    vec3 atmosphere = bedrockAtmosphereActive() ? bedrockSunlight(toSun.y) : sampleTransmittance(radius, sunCos);
    return (VPT_SUN_RADIANCE * worldUBO.sunBrightness) * atmosphere;
}

void main() {
    uint query = shadowRay.pad0;
    if (query == GLOW_OUTLINE_QUERY || query == HAND_CLEARANCE_QUERY) { return; }
    if (query == BLOCK_LIGHT_QUERY || query == SKY_VISIBILITY_QUERY) {
        shadowRay.radiance = vec3(1.0);
        return;
    }
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // An underwater celestial query pays for the water it crossed only once it is known to reach the sky.
    if (query == UNDERWATER_CELESTIAL_QUERY) {
        shadowRay.throughput *= shadowRay.waterTransmission;
    }
#endif

    // A lightning flash takes the sun's or moon's place while it lasts (celestial.glsl).
    float lightning = lightningFlash();
    if (lightning > 1e-3) {
        shadowRay.radiance += LIGHTNING_RADIANCE * lightning * shadowRay.throughput;
        return;
    }

    // The End has no sun or moon; its only sky light is the flash.
    if (worldUBO.skyType == 2) {
        shadowRay.radiance += END_FLASH_RADIANCE * endFlashIntensity() * shadowRay.throughput;
        return;
    }

    vec3 toSun = celestialSunDirection();
    float worldHeight = gl_WorldRayOriginEXT.y + float(worldUBO.cameraPos.y);

#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    const float BEDROCK_SUN_SCALE = 0.8;
    if (bedrockAtmosphereActive()) {
        // Bedrock RTX's own sun/moon light for this time of day (bedrock_atmosphere.glsl), already faded at the
        // horizon, scaled by the Day/Night Brightness settings relative to their defaults (0.25 and 0.35).
        float brightnessScale = toSun.y >= 0.0 ? worldUBO.sunBrightness / 0.25
                                               : worldUBO.moonBrightness / 0.35 * celestialMoonLitFraction();
        // The sun's direct light a little under Bedrock's (BEDROCK_SUN_SCALE): at full strength daylight read harsh,
        // sunlit ground near white. The sky's light is left as it is, so the meter is not handed the whole cut back.
        vec3 bedrockLight = bedrockCelestialLight(toSun.y) * brightnessScale * (toSun.y >= 0.0 ? BEDROCK_SUN_SCALE : 1.0);
        bedrockLight *= biomeFogLightTransmittance(worldHeight, toSun);
        shadowRay.radiance += bedrockLight * shadowRay.throughput;
        return;
    }
#endif

    vec3 light = celestialRadiance(toSun);
    light *= biomeFogLightTransmittance(worldHeight, toSun);

    // Fade the light in over the last 0.3 of height above (or below) the horizon as a quarter sine, so sunrise and
    // moonrise do not switch on. Bedrock's sun table already includes this fade, so its sun is left alone.
    const float horizonBand = 0.3;
    // The height is that of the body giving the light: by night the moon's, whose path can lean apart from the sun's.
    float lightHeight = toSun.y > 0 ? toSun.y : celestialMoonDirection().y;
    bool bedrockSun = bedrockAtmosphereActive() && toSun.y > 0;
    if (abs(lightHeight) < horizonBand && !bedrockSun) {
        light *= sin(PI / (2 * horizonBand) * abs(lightHeight));
    }

    shadowRay.radiance += light * shadowRay.throughput;
}
