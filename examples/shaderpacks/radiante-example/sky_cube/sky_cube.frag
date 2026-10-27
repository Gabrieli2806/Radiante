#version 460
#extension GL_GOOGLE_include_directive : require

// Renders one face (FACE, 0..5 in +x, -x, +y, -y, +z, -z order) of the sky cube map. With the Bedrock-style
// atmosphere active it defers to that model; otherwise it integrates single Rayleigh + Mie scattering along the view
// ray for the sun and the moon, using the transmittance LUT for the light's path to each sample point.

#include "common/shared.hpp"

layout(set = 2, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 2, binding = 2) uniform SkyUniform {
    SkyUBO skyUBO;
};

#include "common/celestial.glsl"

layout(set = 5, binding = 0) uniform sampler2D transLUT;

#include "common/bedrock_atmosphere.glsl"

layout(location = 0) in vec2 texCoord;

layout(location = 0) out vec4 outColor;

// Ray against an origin-centred sphere (unit direction). False on a miss; otherwise both distances, which may be
// negative when the sphere is behind or around the origin.
bool intersectSphere(vec3 rayOrigin, vec3 rayDir, float radius, out float tNear, out float tFar) {
    float halfB = dot(rayOrigin, rayDir);
    float discriminant = halfB * halfB - (dot(rayOrigin, rayOrigin) - radius * radius);
    if (discriminant < 0.0) return false;
    float root = sqrt(discriminant);
    tNear = -halfB - root;
    tFar = -halfB + root;
    return true;
}

// Exponential density profile; below the ground it stays at the sea-level value.
float densityExp(float height, float scaleHeight) {
    return exp(-max(height, 0.0) / scaleHeight);
}

// Transmittance from radius r towards the top of the atmosphere at view cosine mu (the LUT's parameterisation,
// see trans_lut.frag), clamped half a texel inside so the bilinear fetch never wraps.
vec3 sampleTransmittance(float r, float mu) {
    vec2 uv = vec2(clamp(mu * 0.5 + 0.5, 0.0, 1.0),
                   clamp((r - VPT_ATMOSPHERE_RG) / (VPT_ATMOSPHERE_RT - VPT_ATMOSPHERE_RG), 0.0, 1.0));
    vec2 halfTexel = 0.5 / vec2(textureSize(transLUT, 0));
    return texture(transLUT, clamp(uv, halfTexel, 1.0 - halfTexel)).rgb;
}

// Single scattering of one light along the view ray, in 32 midpoint steps between the camera (or the atmosphere
// entry) and the atmosphere exit or the ground. mieScale weighs the aerosol (Mie) part, which draws the glow around
// the light: full for the sun, only a trace for the moon, as with a real moon.
vec3 integrateSingleScattering(vec3 rayOrigin, vec3 rayDir, vec3 lightDir, vec3 lightRadiance, float mieScale) {
    float start, end;
    if (!intersectSphere(rayOrigin, rayDir, VPT_ATMOSPHERE_RT, start, end)) return vec3(0.0);
    start = max(start, 0.0);

    // Stop at the ground if the ray reaches it.
    float groundNear, groundFar;
    if (intersectSphere(rayOrigin, rayDir, VPT_ATMOSPHERE_RG, groundNear, groundFar)) {
        float ground = groundNear > 0.0 ? groundNear : groundFar;
        if (ground > 0.0) end = min(end, ground);
    }

    const int STEP_COUNT = 32;
    float stepLength = (end - start) / float(STEP_COUNT);

    float cosAngle = dot(lightDir, rayDir);
    float phaseRayleigh = 3.0 / (16.0 * PI) * (1.0 + cosAngle * cosAngle);
    // Henyey-Greenstein, with the anisotropy capped at 0.45: at the configured 0.8 the aerosols painted a white disc
    // several times the sun's size around it and the square sun sprite turned into a blurry ball. A broad lobe still
    // warms the sky towards the sun without the halo.
    float g = clamp(min(VPT_ATMOSPHERE_MIE_G, 0.45), -0.999, 0.999);
    float gSquared = g * g;
    float hgDenominator = pow(1.0 + gSquared - 2.0 * g * clamp(cosAngle, -1.0, 1.0) + 1e-6, 1.5);
    float phaseMie = (1.0 - gSquared) / (4.0 * PI * hgDenominator);

    vec3 inscattered = vec3(0.0);
    vec3 towardsCamera = vec3(1.0);
    for (int stepIndex = 0; stepIndex < STEP_COUNT; stepIndex++) {
        vec3 point = rayOrigin + rayDir * (start + (float(stepIndex) + 0.5) * stepLength);
        float radius = length(point);
        float altitude = radius - VPT_ATMOSPHERE_RG;

        vec3 rayleigh = VPT_ATMOSPHERE_BETA_R * densityExp(altitude, VPT_ATMOSPHERE_HR);
        vec3 mie = VPT_ATMOSPHERE_BETA_M * densityExp(altitude, VPT_ATMOSPHERE_HM);
        vec3 sunlight = sampleTransmittance(radius, dot(point / radius, lightDir));

        vec3 scattering = rayleigh * phaseRayleigh + mie * phaseMie * mieScale;
        inscattered += towardsCamera * (sunlight * (scattering * lightRadiance)) * stepLength;
        towardsCamera *= exp(-(rayleigh + mie) * stepLength);
    }
    return inscattered;
}

// View direction of a texel on the current cube face (standard cube-map face orientation).
vec3 cubeFaceDirection(vec2 faceCoord) {
    vec2 c = faceCoord * 2.0 - 1.0;
    switch (FACE) {
        case 0: return normalize(vec3(1, -c.y, -c.x));
        case 1: return normalize(vec3(-1, -c.y, c.x));
        case 2: return normalize(vec3(c.x, 1, c.y));
        case 3: return normalize(vec3(c.x, -1, -c.y));
        case 4: return normalize(vec3(c.x, -c.y, 1));
        default: return normalize(vec3(-c.x, -c.y, -1));
    }
}

// Pushes a low (or below-horizon) light back up to just above the horizon, keeping its compass bearing.
vec3 liftToHorizon(vec3 direction) {
    return normalize(vec3(direction.x, 0.0, direction.z) + vec3(0.0, 0.015, 0.0));
}

void main() {
    vec3 viewDir = cubeFaceDirection(texCoord);
    if (bedrockAtmosphereActive()) {
        outColor = vec4(bedrockSkyRadiance(viewDir, celestialSunDirection()), 1.0);
        return;
    }

    // Directions below the minimum view cosine are flattened towards the horizon (blended over a small band) so the
    // lower half of the cube shows a horizon-coloured sky rather than the dark ground side of the atmosphere.
    float aboveHorizon = smoothstep(-0.05, 0.02, viewDir.y);
    viewDir.y = max(viewDir.y, VPT_ATMOSPHERE_MIN_VIEW_COS);
    viewDir = normalize(mix(vec3(viewDir.x, VPT_ATMOSPHERE_MIN_VIEW_COS, viewDir.z), viewDir, aboveHorizon));

    // Planet-centred origin: ground radius plus the camera height plus a fixed lift.
    vec3 origin = vec3(0.0, VPT_ATMOSPHERE_RG + worldUBO.cameraViewMatInv[3].y + 70.0, 0.0);

    vec3 sunDir = celestialSunDirection();
    vec3 moonDir = celestialMoonDirection();
    vec3 sunLight = VPT_SUN_RADIANCE * worldUBO.sunBrightness;
    vec3 moonLight = VPT_MOON_RADIANCE * worldUBO.moonBrightness * celestialMoonLitFraction();

    // Twilight. Plain single scattering goes black as soon as the sun sets, while the moon is still too low to light
    // anything, which left a grey-black gap darker than midnight before the night blue appeared. Real skies (and
    // Bedrock RTX) keep glowing from light bent around the Earth: here the light is held just above the horizon and
    // faded as it sinks, giving the afterglow and the blue hour.
    float sunFade = 1.0;
    if (sunDir.y < 0.0) {
        float s = smoothstep(-0.32, 0.0, sunDir.y);
        sunFade = s * s;
        sunDir = liftToHorizon(sunDir);
    }
    if (moonDir.y < 0.0) {
        float m = smoothstep(-0.2, 0.0, moonDir.y);
        moonLight *= m * m;
        moonDir = liftToHorizon(moonDir);
    }

    vec3 sky = vec3(0.0);
    if (sunFade > 1e-4) {
        sky += integrateSingleScattering(origin, viewDir, sunDir, sunLight * sunFade, 1.0);
    }
    if (max(moonLight.r, max(moonLight.g, moonLight.b)) > 1e-5) {
        sky += integrateSingleScattering(origin, viewDir, moonDir, moonLight, 0.05);
    }
    outColor = vec4(sky, 1.0);
}
