#ifndef VPT_VOLUMETRIC_CLOUD_IMPL_GLSL
#define VPT_VOLUMETRIC_CLOUD_IMPL_GLSL

// Ray-marched volumetric clouds for the overworld sky (cloud mode 2).
//
// The cloud shell is a spherical layer around a planet whose surface sits at world y = 0 (planet radius RG), from
// BOTTOM_HEIGHT to TOP_HEIGHT above it. It holds three stacked layers, by normalised height in the shell:
//   0.0 - 0.4  cumulus: dense, billowy, the main clouds
//   0.4 - 0.8  alto clouds: thinner, sampled at twice the noise frequency
//   0.8 - 1.0  cirrus: a thin, wispy veil with no detail erosion
// Each layer derives a coverage from a weather map, a slowly drifting coverage noise and the rain state, then a
// density from 3D noise. The view march accumulates single scattering of the dominant light (sun or moon) plus an
// approximated multiple-scattering octave, sky ambient from above and light bounced up from the ground, with light
// transport rays towards the light and upwards that are reused across several steps. The result is finally hazed
// by the atmosphere between camera and cloud and blended into the horizon colour far away.
//
// Callers: pt_miss.glsl (sky rays), pt_direct/pt_froxel/pt_volumetric (cloud shadows via
// volumetricCloudLightVisibility), biome_haze.glsl and water_medium.glsl (the cloud light colour).

#include "common/celestial.glsl"
#include "common/bedrock_atmosphere.glsl"

// ---- Settings (pack attributes; the C++ side may define any of these as compile-time or live values) ----
#ifndef VPT_CLOUD_MODE
#    define VPT_CLOUD_MODE 1u
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_BOTTOM_HEIGHT
#    define VPT_VOLUMETRIC_CLOUD_BOTTOM_HEIGHT 300.0
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_TOP_HEIGHT
#    define VPT_VOLUMETRIC_CLOUD_TOP_HEIGHT 600.0
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_BASE_SCALE
#    define VPT_VOLUMETRIC_CLOUD_BASE_SCALE 0.30
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_DETAIL_SCALE
#    define VPT_VOLUMETRIC_CLOUD_DETAIL_SCALE 0.60
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_COVERAGE
#    define VPT_VOLUMETRIC_CLOUD_COVERAGE 0.50
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_CLEAR_AMOUNT
#    define VPT_VOLUMETRIC_CLOUD_CLEAR_AMOUNT 1u
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_DENSITY
#    define VPT_VOLUMETRIC_CLOUD_DENSITY 1.00
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_VIEW_STEPS
#    define VPT_VOLUMETRIC_CLOUD_VIEW_STEPS 128
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_LIGHT_STEPS
#    define VPT_VOLUMETRIC_CLOUD_LIGHT_STEPS 12
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_CAST_SHADOW
#    define VPT_VOLUMETRIC_CLOUD_CAST_SHADOW 1
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_AMBIENT_STEPS
#    define VPT_VOLUMETRIC_CLOUD_AMBIENT_STEPS 8
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_AMBIENT_STRENGTH
#    define VPT_VOLUMETRIC_CLOUD_AMBIENT_STRENGTH 1.0
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_POWDER_STRENGTH
#    define VPT_VOLUMETRIC_CLOUD_POWDER_STRENGTH 1.0
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_WEATHER_SCALE
#    define VPT_VOLUMETRIC_CLOUD_WEATHER_SCALE 0.020
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_SHADOW_STRENGTH
#    define VPT_VOLUMETRIC_CLOUD_SHADOW_STRENGTH 0.80
#endif
#ifndef VPT_VOLUMETRIC_CLOUD_SHADOW_SOFTNESS
#    define VPT_VOLUMETRIC_CLOUD_SHADOW_SOFTNESS 0.50
#endif
// Cheaper step budgets for clouds seen through reflections and refractions.
#ifndef VPT_INDIRECT_VOLUMETRIC_CLOUD_VIEW_STEPS
#    define VPT_INDIRECT_VOLUMETRIC_CLOUD_VIEW_STEPS 20
#endif
#ifndef VPT_INDIRECT_VOLUMETRIC_CLOUD_LIGHT_STEPS
#    define VPT_INDIRECT_VOLUMETRIC_CLOUD_LIGHT_STEPS 5
#endif
#ifndef VPT_INDIRECT_VOLUMETRIC_CLOUD_AMBIENT_STEPS
#    define VPT_INDIRECT_VOLUMETRIC_CLOUD_AMBIENT_STEPS 3
#endif

// Values of VPT_VOLUMETRIC_CLOUD_CLEAR_AMOUNT (how cloudy clear weather is).
#define VPT_VOLUMETRIC_CLOUD_CLEAR_AMOUNT_FEW 0u
#define VPT_VOLUMETRIC_CLOUD_CLEAR_AMOUNT_MEDIUM 1u
#define VPT_VOLUMETRIC_CLOUD_CLEAR_AMOUNT_MANY 2u

layout(set = 5, binding = 22) uniform sampler3D volumetricCloudBasicNoiseTexture;
layout(set = 5, binding = 23) uniform sampler3D volumetricCloudDetailNoiseTexture;
layout(set = 5, binding = 24) uniform sampler2D volumetricCloudWeatherTexture;
layout(set = 5, binding = 25) uniform sampler2D volumetricCloudCurlTexture;
layout(set = 5, binding = 26) uniform sampler2D volumetricCloudCoverageNoiseTexture;

// What a cloud march hands back: the radiance seen along the ray (background included), how much of the
// background still shows through, and 1 when any cloud was met.
struct VolumetricCloudResult {
    vec3 color;
    float transmittance;
    float hit;
};

struct VolumetricCloudAtmosphereSegmentResult {
    vec3 scatteredLight;
    vec3 transmittance;
};

// Everything the density lookup needs about one sample point, worked out once per point.
struct VolumetricCloudSampleContext {
    vec3 posKm;          // noise-space position (km, warped by curl noise)
    vec3 windOffset;     // wind scroll added to the noise lookups
    float coverage;      // 0..1 cloud coverage at this column
    float gradientShape; // vertical profile within the layer
    float normalizedHeight;
    float layerHeight01; // 0..1 height inside the layer
    int layerIndex;      // 0, 1, 2, or -1 outside the shell
};

// Wind: a fixed heading and speed for all layers.
const float VCL_WIND_SPEED = 0.05;

// ---- Small math helpers ----

float cloudSaturate(float value) {
    return clamp(value, 0.0, 1.0);
}

// Linear remap of [inMin, inMax] onto [outMin, outMax], clamped at both ends.
float cloudRemapClamped(float value, float inMin, float inMax, float outMin, float outMax) {
    float t = clamp((value - inMin) / max(inMax - inMin, 1e-5), 0.0, 1.0);
    return mix(outMin, outMax, t);
}

vec3 cloudSafeNormalize(vec3 value, vec3 fallback) {
    float lengthSquared = dot(value, value);
    if (lengthSquared <= 1e-8 || any(isnan(value)) || any(isinf(value))) { return fallback; }
    return value * inversesqrt(lengthSquared);
}

// Cheap 2D -> 1D hash (fract-of-products), only used to jitter where the march starts.
float cloudHash12(vec2 p) {
    vec3 h = fract(vec3(p.xyx) * 0.1031);
    h += dot(h, h.yzx + 33.33);
    return fract((h.x + h.y) * h.z);
}

// Ray against an origin-centred sphere (unit direction). False on a miss; otherwise both distances.
bool cloudIntersectSphere(vec3 ro, vec3 rd, float radius, out float tNear, out float tFar) {
    float halfB = dot(ro, rd);
    float discriminant = halfB * halfB - (dot(ro, ro) - radius * radius);
    if (discriminant < 0.0) { return false; }
    float root = sqrt(discriminant);
    tNear = -halfB - root;
    tFar = -halfB + root;
    return true;
}

// ---- Coordinate frames ----

// Ray positions are camera-relative; the noise is anchored to absolute world coordinates.
vec3 cloudAbsoluteWorldPos(vec3 relativeWorldPos) {
    return relativeWorldPos + vec3(worldUBO.cameraPos.xyz);
}

// Planet-centred position: world y = 0 is the planet surface.
vec3 cloudPlanetPos(vec3 relativeWorldPos) {
    vec3 world = cloudAbsoluteWorldPos(relativeWorldPos);
    return vec3(world.x, VPT_ATMOSPHERE_RG + world.y, world.z);
}

float cloudBottomRadius() {
    return VPT_ATMOSPHERE_RG + VPT_VOLUMETRIC_CLOUD_BOTTOM_HEIGHT;
}

float cloudTopRadius() {
    return VPT_ATMOSPHERE_RG + VPT_VOLUMETRIC_CLOUD_TOP_HEIGHT;
}

// Height through the cloud shell, 0 at its floor and 1 at its ceiling.
float cloudHeight01(vec3 planetPos) {
    float shellThickness = max(cloudTopRadius() - cloudBottomRadius(), 1e-3);
    return clamp((length(planetPos) - cloudBottomRadius()) / shellThickness, 0.0, 1.0);
}

float cloudTime() {
    return worldUBO.gameTime;
}

vec3 cloudWindDirection() {
    return cloudSafeNormalize(vec3(0.8, 0.0, 0.4), vec3(1.0, 0.0, 0.0));
}

// The part of a ray inside the cloud shell, in front of the origin and above the ground. When the ray crosses
// the hollow below the shell (looking down from above, or up from below), only the first piece is kept.
bool intersectCloudLayer(vec3 rayOrigin, vec3 rayDir, out float tEnter, out float tExit) {
    vec3 origin = cloudPlanetPos(rayOrigin);
    float outerNear, outerFar;
    if (!cloudIntersectSphere(origin, rayDir, cloudTopRadius(), outerNear, outerFar)) { return false; }
    tEnter = max(outerNear, 0.0);
    tExit = max(outerFar, 0.0);
    if (tExit <= tEnter) { return false; }

    float innerNear, innerFar;
    if (!cloudIntersectSphere(origin, rayDir, cloudBottomRadius(), innerNear, innerFar)) {
        // Misses the shell's floor, so it misses the (smaller) ground sphere too.
        return tExit > tEnter;
    }
    if (innerFar > 0.0) {
        if (innerNear > 0.0 && tEnter < innerNear) {
            // Coming down from above: stop where the ray leaves the shell through its floor.
            tExit = min(tExit, innerNear);
        } else {
            // Starting below (or inside) the floor: begin where the ray comes out of it.
            tEnter = max(tEnter, innerFar);
        }
    }

    float groundNear, groundFar;
    if (cloudIntersectSphere(origin, rayDir, VPT_ATMOSPHERE_RG, groundNear, groundFar)) {
        float ground = groundNear > 0.0 ? groundNear : groundFar;
        if (ground > 0.0) { tExit = min(tExit, ground); }
    }
    return tExit > tEnter;
}

float volumetricCloudSegmentLength(vec3 rayOrigin, vec3 rayDir) {
    float tEnter, tExit;
    if (!intersectCloudLayer(rayOrigin, rayDir, tEnter, tExit)) { return 0.0; }
    return max(tExit - tEnter, 0.0);
}

// Where a ray enters the cloud shell and how far it travels inside; false when that is (almost) nothing.
bool volumetricCloudTraceSegment(vec3 rayOrigin, vec3 rayDir, out vec3 segmentOrigin, out float segmentLength) {
    float tEnter, tExit;
    if (!intersectCloudLayer(rayOrigin, rayDir, tEnter, tExit)) {
        segmentOrigin = rayOrigin;
        segmentLength = 0.0;
        return false;
    }
    segmentOrigin = rayOrigin + rayDir * tEnter;
    segmentLength = max(tExit - tEnter, 0.0);
    return segmentLength > 1e-3;
}

// ---- Atmosphere ----

// The transmittance LUT's parameterisation (see trans_lut.frag).
vec2 transmittanceUv(float r, float mu) {
    float u = clamp(mu * 0.5 + 0.5, 0.0, 1.0);
    float v = clamp((r - VPT_ATMOSPHERE_RG) / max(VPT_ATMOSPHERE_RT - VPT_ATMOSPHERE_RG, 1.0), 0.0, 1.0);
    return vec2(u, v);
}

vec3 sampleCloudAtmosphereTransmittance(float r, float mu) {
    return sampleTexture(transLUT, transmittanceUv(r, mu), false).rgb;
}

// ---- Weather ----

float volumetricCloudRainBlend() {
    return skyUBO.rainGradient;
}

// Ramps to 1 early in the rain fade, so the overcast deck forms before the rain is at full strength.
float volumetricCloudOvercastBlend() {
    return smoothstep(0.02, 0.20, volumetricCloudRainBlend());
}

float volumetricCloudClearAmountScale() {
    switch (VPT_VOLUMETRIC_CLOUD_CLEAR_AMOUNT) {
        case VPT_VOLUMETRIC_CLOUD_CLEAR_AMOUNT_FEW: return 0.82;
        case VPT_VOLUMETRIC_CLOUD_CLEAR_AMOUNT_MANY: return 1.22;
        default: return 1.0;
    }
}

// Clear-weather coverage breathes by up to +-8% over in-game days, from three slow, unrelated sine waves.
float volumetricCloudClearCoverageDrift() {
    float ticks = cloudTime() * 24000.0;
    float drift = 0.50 * sin(ticks * 0.00013) + 0.35 * sin(ticks * 0.000057 + 1.7) + 0.15 * sin(ticks * 0.000031 - 0.8);
    return 1.0 + 0.08 * drift;
}

// Coverage noise at two scales, blended so cloud fields merge into larger groups instead of an even scatter.
float volumetricCloudMergedCoverageNoise(vec2 animatedWorldXZ, float scale, vec2 offset) {
    float fine = texture(volumetricCloudCoverageNoiseTexture, animatedWorldXZ * scale + offset).x;
    float broad =
        texture(volumetricCloudCoverageNoiseTexture, animatedWorldXZ * (scale * 0.57) + offset + vec2(0.173, -0.121)).x;
    return mix(fine, broad, 0.55);
}

float volumetricCloudCoverageBase(float clearBase, float rainyBase) {
    return cloudSaturate(mix(clearBase, rainyBase, volumetricCloudOvercastBlend()));
}

// Weather-map coverage: an affine remap in clear weather, raised to at least rainyFloor as it gets overcast.
float volumetricCloudWeatherSignal(float weatherValue, float clearScale, float clearBias, float rainyFloor) {
    float clearSignal = cloudSaturate(weatherValue * clearScale + clearBias);
    return cloudSaturate(mix(clearSignal, max(clearSignal, rainyFloor), volumetricCloudOvercastBlend()));
}

// ---- Lighting inputs ----

// Sky radiance in a direction, greyed towards a flat overcast colour as the rain sets in.
vec3 volumetricCloudSkyRadiance(vec3 sampleDir) {
    float dayAmount = smoothstep(-0.3, 0.3, celestialSunDirection().y);
    vec3 overcast = mix(vec3(0.03, 0.035, 0.04), vec3(0.12, 0.13, 0.14), dayAmount);
    return mix(texture(skyFull, sampleDir).rgb, overcast, volumetricCloudRainBlend());
}

// How much direct light the sun / moon gives the clouds: none until it is a little above the horizon.
float volumetricCloudSunDirectScale() {
    return smoothstep(0.03, 0.14, celestialSunDirection().y);
}

float volumetricCloudMoonDirectScale() {
    return smoothstep(0.03, 0.14, celestialMoonDirection().y);
}

// The light that dominates the clouds right now: the brighter of sun and moon, or whichever is up when neither
// gives direct light yet.
vec3 volumetricCloudPrimaryLightDir() {
    vec3 sunDir = celestialSunDirection();
    vec3 moonDir = celestialMoonDirection();
    float sunScale = volumetricCloudSunDirectScale();
    float moonScale = volumetricCloudMoonDirectScale();
    if (max(sunScale, moonScale) <= 1e-4) { return sunDir.y >= 0.0 ? sunDir : moonDir; }
    return sunScale >= moonScale ? sunDir : moonDir;
}

// Radiance of that light, dimmed by rain.
vec3 volumetricCloudPrimaryLightRadiance() {
    float sunScale = volumetricCloudSunDirectScale();
    float moonScale = volumetricCloudMoonDirectScale();
    float rainDimming = mix(1.0, 0.35, volumetricCloudRainBlend());
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    if (bedrockAtmosphereActive()) {
        // Bedrock RTX's sun or moon, the same light shadow.rmiss gives surfaces.
        float sunHeight = celestialSunDirection().y;
        float brightness = sunHeight >= 0.0 ? worldUBO.sunBrightness / 0.25
                                            : worldUBO.moonBrightness / 0.35 * celestialMoonLitFraction();
        return bedrockCelestialLight(sunHeight) * brightness * rainDimming;
    }
#endif
    if (sunScale >= moonScale) {
        return (VPT_SUN_RADIANCE * worldUBO.sunBrightness) * sunScale * rainDimming;
    }
    return (VPT_MOON_RADIANCE * worldUBO.moonBrightness * celestialMoonLitFraction()) * moonScale * rainDimming;
}

float atmosphereDensityExp(float height, float scaleHeight) {
    return exp(-max(height, 0.0) / max(scaleHeight, 1e-3));
}

float phaseRayleigh(float cosTheta) {
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

// Henyey-Greenstein phase with its inputs clamped to safe ranges.
float phaseMieHG(float cosTheta, float g) {
    float c = clamp(cosTheta, -1.0, 1.0);
    float gc = clamp(g, -0.999, 0.999);
    float gSquared = gc * gc;
    float denominator = 1.0 + gSquared - 2.0 * gc * c;
    return (1.0 - gSquared) / (4.0 * PI * pow(max(denominator, 1e-6), 1.5));
}

// Aerial perspective between the camera and the clouds: Rayleigh + Mie single scattering of the cloud light and
// the transmittance over up to maxDistance (0 = to the edge of the atmosphere), in 8 midpoint steps.
VolumetricCloudAtmosphereSegmentResult integrateAtmosphereSegment(vec3 rayOrigin, vec3 rayDir, float maxDistance) {
    VolumetricCloudAtmosphereSegmentResult air;
    air.scatteredLight = vec3(0.0);
    air.transmittance = vec3(1.0);

    vec3 origin = cloudPlanetPos(rayOrigin);
    float topNear, topFar;
    if (!cloudIntersectSphere(origin, rayDir, VPT_ATMOSPHERE_RT, topNear, topFar)) { return air; }
    float start = max(topNear, 0.0);
    float end = max(topFar, 0.0);

    float groundNear, groundFar;
    if (cloudIntersectSphere(origin, rayDir, VPT_ATMOSPHERE_RG, groundNear, groundFar)) {
        float ground = groundNear > 0.0 ? groundNear : groundFar;
        if (ground > 0.0) { end = min(end, ground); }
    }
    if (maxDistance > 0.0) { end = min(end, maxDistance); }
    if (end <= start) { return air; }

    vec3 lightDir = volumetricCloudPrimaryLightDir();
    vec3 lightRadiance = volumetricCloudPrimaryLightRadiance();
    float cosAngle = dot(lightDir, rayDir);
    float rayleighPhase = phaseRayleigh(cosAngle);
    float miePhase = phaseMieHG(cosAngle, VPT_ATMOSPHERE_MIE_G);

    const int STEP_COUNT = 8;
    float stepLength = (end - start) / max(float(STEP_COUNT), 1.0);
    for (int stepIndex = 0; stepIndex < STEP_COUNT; ++stepIndex) {
        vec3 point = origin + rayDir * (start + (float(stepIndex) + 0.5) * stepLength);
        float radius = length(point);
        float altitude = radius - VPT_ATMOSPHERE_RG;
        float lightCos = dot(point / max(radius, 1e-4), lightDir);

        vec3 rayleigh = VPT_ATMOSPHERE_BETA_R * atmosphereDensityExp(altitude, VPT_ATMOSPHERE_HR);
        vec3 mie = VPT_ATMOSPHERE_BETA_M * atmosphereDensityExp(altitude, VPT_ATMOSPHERE_HM);
        vec3 sunlight = sampleCloudAtmosphereTransmittance(radius, lightCos);
        vec3 scattering = rayleigh * rayleighPhase + mie * miePhase;

        air.scatteredLight += air.transmittance * (sunlight * scattering * lightRadiance) * stepLength;
        air.transmittance *= exp(-(rayleigh + mie) * stepLength);
    }
    return air;
}

// Henyey-Greenstein written with cosTheta measured against the light's travel direction (note the + sign).
float hgPhase(float g, float cosTheta) {
    float numerator = 1.0 - g * g;
    float denominator = 1.0 + g * g + 2.0 * g * cosTheta;
    return numerator / (4.0 * PI * denominator * sqrt(max(denominator, 1e-5)));
}

// Forward and backward lobes mixed by w: clouds both glow towards the sun and keep a silver lining away from it.
float dualLobePhase(float g0, float g1, float w, float cosTheta) {
    return mix(hgPhase(g0, cosTheta), hgPhase(g1, cosTheta), w);
}

// "Powder" darkening of thin cloud edges seen away from the light: the height term is relaxed towards 1 as the
// view turns side-on to the light, then scaled by the depth term.
float powderEffectNew(float depth, float height, float viewToLight) {
    float sideOn = -abs(viewToLight) * 0.5 + 0.5;
    sideOn = sideOn * sideOn;
    return depth * (height * (1.0 - sideOn) + sideOn);
}

// ---- Cloud layers ----

VolumetricCloudSampleContext invalidCloudSampleContext() {
    VolumetricCloudSampleContext context;
    context.posKm = vec3(0.0);
    context.windOffset = vec3(0.0);
    context.coverage = 0.0;
    context.gradientShape = 0.0;
    context.normalizedHeight = 0.0;
    context.layerHeight01 = 0.0;
    context.layerIndex = -1;
    return context;
}

// Shared first half of every layer: shears the sample downwind with height (so tops lean with the wind), warps
// it with curl noise into the noise-space position and sets the wind scroll. Returns the sheared position in
// metres and, in weatherKmXZ, where the weather map is read; that point is pulled back towards the unwarped
// position as it gets overcast so a storm deck does not churn.
vec3 vclLayerFrame(inout VolumetricCloudSampleContext context, vec3 sampleWorldPos, float layerHeight01,
                   float curlScale, float curlOffset, float curlStrength, out vec2 weatherKmXZ) {
    vec3 windDirection = cloudWindDirection();
    float time = cloudTime();
    vec3 posMeter = cloudAbsoluteWorldPos(sampleWorldPos);
    posMeter += windDirection * layerHeight01 * 500.0;
    context.posKm = posMeter * 0.001;

    vec3 curl = texture(volumetricCloudCurlTexture, (time * VCL_WIND_SPEED * 50.0 + posMeter.xz) * curlScale + curlOffset).xyz;
    context.posKm += (curl * 2.0 - 1.0) * curlStrength;

    context.windOffset = (windDirection + vec3(0.0, 0.1, 0.0)) * time * VCL_WIND_SPEED;
    weatherKmXZ = mix(context.posKm.xz, posMeter.xz * 0.001, volumetricCloudOvercastBlend() * 0.85);
    return posMeter;
}

// Coverage in clear weather drifts with time and the clear-amount setting; overcast pushes it to a fixed value.
float vclLayerCoverageBase(float clearShare, float rainyBase) {
    float drift = mix(volumetricCloudClearCoverageDrift(), 1.0, volumetricCloudOvercastBlend());
    return volumetricCloudCoverageBase(VPT_VOLUMETRIC_CLOUD_COVERAGE * clearShare * drift * volumetricCloudClearAmountScale(),
                                       rainyBase);
}

// Scrolling position for the coverage noise; rain slows the scroll to a tenth.
vec2 vclCoverageScroll(vec3 posMeter, float rainSlowdown) {
    return posMeter.xz + vec2(cloudTime() * VCL_WIND_SPEED * 50.0 * rainSlowdown);
}

void vclFinishContext(inout VolumetricCloudSampleContext context, float normalizedHeight, float layerHeight01,
                      int layerIndex) {
    context.normalizedHeight = normalizedHeight;
    context.layerHeight01 = layerHeight01;
    context.layerIndex = layerIndex;
}

// Layer 0, cumulus.
VolumetricCloudSampleContext prepareCloudLayer0(vec3 sampleWorldPos, float normalizedHeight, float layerHeight01) {
    float overcast = volumetricCloudOvercastBlend();
    float coverageBase = vclLayerCoverageBase(0.92, max(VPT_VOLUMETRIC_CLOUD_COVERAGE + 0.12, 0.72));

    VolumetricCloudSampleContext context = invalidCloudSampleContext();
    vec2 weatherKmXZ;
    vec3 posMeter = vclLayerFrame(context, sampleWorldPos, layerHeight01, 0.0000008, 0.7, 2.0, weatherKmXZ);

    float weather = texture(volumetricCloudWeatherTexture, weatherKmXZ * VPT_VOLUMETRIC_CLOUD_WEATHER_SCALE).x;
    float weatherCoverage = volumetricCloudWeatherSignal(weather, 0.72, -0.04, 0.58);
    float noiseCoverage = volumetricCloudMergedCoverageNoise(vclCoverageScroll(posMeter, mix(1.0, 0.10, overcast)),
                                                             0.00000060, vec2(0.50));
    noiseCoverage = cloudRemapClamped(noiseCoverage, 0.34, 0.76, 0.0, 1.0) * mix(0.18, 0.22, overcast);

    float combined = max(cloudSaturate(noiseCoverage + weatherCoverage), overcast * 0.60);
    context.coverage = cloudSaturate(coverageBase * combined);
    // Wide, flat bases tapering towards the top; the taper starts later where coverage is high.
    context.gradientShape = cloudRemapClamped(layerHeight01, 0.10, 0.80, coverageBase * 1.9, 0.2) *
                            cloudRemapClamped(layerHeight01, 0.00, 0.10, 0.5, 1.0);
    vclFinishContext(context, normalizedHeight, layerHeight01, 0);
    return context;
}

// Layer 1, alto clouds.
VolumetricCloudSampleContext prepareCloudLayer1(vec3 sampleWorldPos, float normalizedHeight, float layerHeight01) {
    float overcast = volumetricCloudOvercastBlend();
    float coverageBase = vclLayerCoverageBase(0.60, max(VPT_VOLUMETRIC_CLOUD_COVERAGE + 0.08, 0.54));

    VolumetricCloudSampleContext context = invalidCloudSampleContext();
    vec2 weatherKmXZ;
    vec3 posMeter = vclLayerFrame(context, sampleWorldPos, layerHeight01, 0.000001, -0.3, 5.0, weatherKmXZ);

    // Its own region of the weather map, stretched along one axis into bands.
    vec2 weatherUv = weatherKmXZ * VPT_VOLUMETRIC_CLOUD_WEATHER_SCALE * 0.5 + 0.39;
    weatherUv.y *= 2.0;
    float weatherCoverage =
        volumetricCloudWeatherSignal(texture(volumetricCloudWeatherTexture, weatherUv).x, 0.56, -0.12, 0.34);
    float noiseCoverage = volumetricCloudMergedCoverageNoise(vclCoverageScroll(posMeter, mix(1.0, 0.10, overcast)),
                                                             0.00000072, vec2(-0.11, -0.11));
    noiseCoverage = cloudRemapClamped(noiseCoverage, 0.38, 0.78, 0.0, 1.0) * mix(0.24, 0.28, overcast);

    float combined = max(cloudSaturate(noiseCoverage + weatherCoverage), overcast * 0.36);
    context.coverage = cloudSaturate(coverageBase * combined);
    context.gradientShape = cloudRemapClamped(layerHeight01, 0.00, 0.01, 0.1, 1.0) *
                            cloudRemapClamped(layerHeight01, 0.10, 0.80, 0.7, 0.2);
    vclFinishContext(context, normalizedHeight, layerHeight01, 1);
    return context;
}

// Layer 2, cirrus.
VolumetricCloudSampleContext prepareCloudLayer2(vec3 sampleWorldPos, float normalizedHeight, float layerHeight01) {
    float overcast = volumetricCloudOvercastBlend();
    float coverageBase = vclLayerCoverageBase(0.46, VPT_VOLUMETRIC_CLOUD_COVERAGE * 0.22);

    VolumetricCloudSampleContext context = invalidCloudSampleContext();
    vec2 weatherKmXZ;
    vec3 posMeter = vclLayerFrame(context, sampleWorldPos, layerHeight01, 0.00000125, 0.7, 10.0, weatherKmXZ);

    // Long, thin streaks: the weather map is stretched six-fold along one axis.
    vec2 weatherUv = weatherKmXZ * VPT_VOLUMETRIC_CLOUD_WEATHER_SCALE * 0.6 + 0.739;
    weatherUv.y *= 6.0;
    float weatherCoverage =
        volumetricCloudWeatherSignal(texture(volumetricCloudWeatherTexture, weatherUv).x, 0.35, -0.30, 0.0);
    // Sharpened noise (1 - n^8): mostly full coverage with occasional gaps.
    float noise = texture(volumetricCloudCoverageNoiseTexture,
                          (cloudTime() * VCL_WIND_SPEED * 50.0 + posMeter.xz) * 0.000001 - 0.39).x;
    float noiseCoverage = cloudSaturate(1.0 - pow(noise, 8.0)) * mix(0.26, 0.12, overcast);

    context.coverage = cloudSaturate(coverageBase * (noiseCoverage + weatherCoverage));
    context.gradientShape = cloudRemapClamped(layerHeight01, 0.00, 0.01, 0.1, 1.0) *
                            cloudRemapClamped(layerHeight01, 0.10, 0.20, 0.8, 0.5);
    vclFinishContext(context, normalizedHeight, layerHeight01, 2);
    return context;
}

// Picks the layer a point falls in and prepares its context; outside the shell the context is invalid.
VolumetricCloudSampleContext sampleCloudContext(vec3 sampleWorldPos) {
    float h = cloudHeight01(cloudPlanetPos(sampleWorldPos));
    if (h <= 0.0 || h >= 1.0) { return invalidCloudSampleContext(); }
    if (h < 0.4) { return prepareCloudLayer0(sampleWorldPos, h, h / 0.4); }
    if (h < 0.8) { return prepareCloudLayer1(sampleWorldPos, h, (h - 0.4) / 0.4); }
    return prepareCloudLayer2(sampleWorldPos, h, (h - 0.8) / 0.2);
}

// A cheap upper bound on whether a point can hold cloud, used to stride through empty sky.
float sampleCloudMacroOccupancyFromContext(VolumetricCloudSampleContext context) {
    if (context.layerIndex < 0) { return 0.0; }
    return cloudSaturate(context.coverage * max(cloudSaturate(context.gradientShape), 0.35));
}

// Coverage-eroded base noise: the noise survives only where it exceeds 1 - coverage.
float vclCoveredBase(VolumetricCloudSampleContext context, vec3 baseNoiseCoord) {
    float shaped = context.gradientShape * texture(volumetricCloudBasicNoiseTexture, fract(baseNoiseCoord)).r;
    return context.coverage * cloudRemapClamped(shaped, 1.0 - context.coverage, 1.0, 0.0, 1.0);
}

// Vertical density profile: fades in over the bottom 30% of the layer and out over the top 30%, and thins with
// height overall.
float vclVerticalProfile(float layerHeight01, float scale) {
    return cloudSaturate(0.01 + (1.0 - layerHeight01) * 0.5) * scale *
           cloudRemapClamped(layerHeight01, 0.0, 0.3, 0.0, 1.0) *
           cloudRemapClamped(layerHeight01, 0.7, 1.0, 1.0, 0.0);
}

// Billowy layers (0 and 1): base noise eroded by detail noise, which is inverted above the lowest tenth so the
// bottoms come out wispy and the tops cauliflower-like. frequency doubles the noise scale for the alto layer.
float vclBillowDensity(VolumetricCloudSampleContext context, float frequency, float profileScale, float densityBase) {
    float base = vclCoveredBase(context, frequency * (context.posKm + context.windOffset) * VPT_VOLUMETRIC_CLOUD_BASE_SCALE);

    vec3 detailCoord = context.posKm - context.windOffset * 0.15;
    float detail = texture(volumetricCloudDetailNoiseTexture, fract(frequency * detailCoord * VPT_VOLUMETRIC_CLOUD_DETAIL_SCALE)).r;
    float erosion = 0.2 * mix(detail, 1.0 - detail, cloudSaturate(context.layerHeight01 * 10.0));

    float density = vclVerticalProfile(context.layerHeight01, profileScale) *
                    cloudRemapClamped(base, erosion, 1.0, 0.0, 1.0);
    // Lower parts get a smaller exponent, filling them out; the tops stay soft.
    density = pow(density, cloudSaturate(1.0 - context.layerHeight01) * 0.4 + 0.1) * densityBase * 0.1;
    return cloudSaturate(density);
}

// Extinction coefficient at a prepared point.
float sampleCloudDensityFromContext(VolumetricCloudSampleContext context) {
    float overcast = volumetricCloudOvercastBlend();
    switch (context.layerIndex) {
        case 0:
            return vclBillowDensity(context, 1.0, 0.25, VPT_VOLUMETRIC_CLOUD_DENSITY * mix(1.60, 2.10, overcast));
        case 1:
            return vclBillowDensity(context, 2.0, 0.1, VPT_VOLUMETRIC_CLOUD_DENSITY * mix(0.26, 0.40, overcast));
        case 2: {
            // Cirrus: base noise only, at three times the frequency, no detail erosion.
            float densityBase = VPT_VOLUMETRIC_CLOUD_DENSITY * mix(0.12, 0.09, overcast);
            float base = vclCoveredBase(context, 3.0 * (context.posKm + context.windOffset + 0.39) * VPT_VOLUMETRIC_CLOUD_BASE_SCALE);
            return cloudSaturate(vclVerticalProfile(context.layerHeight01, 0.1) * base * densityBase);
        }
        default:
            return 0.0;
    }
}

// How many steps to stride over when a point looks empty. Before the first cloud is met the march may stride
// further; once inside a cloud it moves more carefully so it does not skip past thin wisps.
int cloudSkipStepCount(float macroOccupancy, bool beforeFirstHit) {
    if (macroOccupancy < 0.004) { return beforeFirstHit ? 6 : 3; }
    if (macroOccupancy < 0.012) { return beforeFirstHit ? 4 : 2; }
    return macroOccupancy < 0.025 ? 2 : 1;
}

// Phase for the single-scattering octave (x) and the multiple-scattering octave (y), which is halfway to
// isotropic because repeated scattering forgets the light's direction.
vec2 getParticipatingMediaPhase(float basePhase) {
    float isotropic = 1.0 / (4.0 * PI);
    return vec2(basePhase, mix(isotropic, basePhase, 0.5));
}

// Transmittance through the clouds along a light ray: x for single scattering, y for the multiple-scattering
// octave, whose extinction is scaled down by multiScatterExtinction (light that scatters on keeps going).
vec2 marchCloudLightTransport(vec3 rayOrigin, vec3 rayDir, float maxDistance, int minimumSteps, float multiScatterExtinction) {
    if (maxDistance <= 1e-3 || minimumSteps <= 0) { return vec2(1.0); }

    int stepCount = max(minimumSteps, 1);
    float stepLength = maxDistance / max(float(stepCount), 1.0);
    float opticalDepth = 0.0;
    for (int stepIndex = 0; stepIndex < stepCount; ++stepIndex) {
        VolumetricCloudSampleContext context = sampleCloudContext(rayOrigin + rayDir * (stepLength * (float(stepIndex) + 0.5)));
        int stride = cloudSkipStepCount(sampleCloudMacroOccupancyFromContext(context), false);
        if (stride > 1) {
            // Skipped steps count as empty.
            stepIndex += min(stride - 1, stepCount - 1 - stepIndex);
            continue;
        }
        float density = sampleCloudDensityFromContext(context);
        if (density > 1e-5) { opticalDepth += density * stepLength; }
    }
    return vec2(exp(-opticalDepth), exp(-opticalDepth * multiScatterExtinction));
}

// Direction in which the soft-shadow taps are offset: a fixed blend of two axes perpendicular to the light.
vec3 vclShadowOffsetDirection(vec3 lightDir) {
    vec3 tangent = cloudSafeNormalize(cross(lightDir, vec3(0.0, 1.0, 0.0)), vec3(1.0, 0.0, 0.0));
    vec3 bitangent = cloudSafeNormalize(cross(lightDir, tangent), vec3(0.0, 0.0, 1.0));
    return cloudSafeNormalize(tangent + bitangent * 0.5, tangent);
}

// Fraction of the light reaching a world position through the clouds (cloud shadows on the ground and in fog).
// With softness, two extra rays offset to either side are blended in (half the steps each), which blurs the shadow
// edge. Shadows fade out as the light approaches the horizon.
float volumetricCloudLightVisibility(vec3 absoluteWorldPos, vec3 lightDir, int minimumSteps, float multiScatterExtinction) {
    if (VPT_CLOUD_MODE != 2u || VPT_VOLUMETRIC_CLOUD_CAST_SHADOW == 0) { return 1.0; }

    float shadowWeight = smoothstep(0.06, 0.24, max(lightDir.y, 0.0)) * clamp(VPT_VOLUMETRIC_CLOUD_SHADOW_STRENGTH, 0.0, 1.0);
    float softness = clamp(VPT_VOLUMETRIC_CLOUD_SHADOW_SOFTNESS, 0.0, 1.0);
    if (shadowWeight <= 1e-4) { return 1.0; }

    vec3 start;
    float distanceInCloud;
    vec3 offsetOrigin = absoluteWorldPos - vec3(worldUBO.cameraPos.xyz) + lightDir * 5.0;
    if (!volumetricCloudTraceSegment(offsetOrigin, lightDir, start, distanceInCloud)) { return 1.0; }

    float visibility = marchCloudLightTransport(start, lightDir, distanceInCloud, max(minimumSteps, 1), multiScatterExtinction).x;
    if (softness > 1e-4) {
        vec3 offset = vclShadowOffsetDirection(lightDir) * (clamp(distanceInCloud * 0.04, 40.0, 450.0) * softness);
        int tapSteps = max(minimumSteps / 2, 1);

        float sideA = 1.0;
        vec3 sideStart;
        float sideDistance;
        if (volumetricCloudTraceSegment(start + offset, lightDir, sideStart, sideDistance)) {
            sideA = marchCloudLightTransport(sideStart, lightDir, sideDistance, tapSteps, multiScatterExtinction).x;
        }
        float sideB = 1.0;
        if (volumetricCloudTraceSegment(start - offset, lightDir, sideStart, sideDistance)) {
            sideB = marchCloudLightTransport(sideStart, lightDir, sideDistance, tapSteps, multiScatterExtinction).x;
        }
        float blurred = visibility * 0.50 + (sideA + sideB) * 0.25;
        visibility = mix(visibility, blurred, softness);
    }
    return mix(1.0, visibility, shadowWeight);
}

vec3 volumetricCloudSkyFullHorizonBase(vec3 rayDir);
vec3 volumetricCloudSkyFullBelowHorizonBase(vec3 rayDir);

vec3 volumetricCloudRainOvercastBase() {
    return mix(vec3(0.045, 0.050, 0.055), vec3(0.12, 0.13, 0.14), smoothstep(-0.3, 0.3, celestialSunDirection().y));
}

// Turns a clear-sky colour into its rainy counterpart as the rain sets in: mostly desaturated, then mostly
// replaced by a flat overcast grey scaled by darkness.
vec3 volumetricCloudRainFilter(vec3 clearSky, float darkness) {
    float luma = dot(max(clearSky, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
    vec3 rainy = mix(mix(clearSky, vec3(luma), 0.72), volumetricCloudRainOvercastBase() * darkness, 0.85);
    return mix(clearSky, rainy, volumetricCloudRainBlend());
}

// Sky light falling on a cloud from above: the sky straight up plus two horizon samples.
vec3 sampleCloudAmbientIrradiance(vec3 up) {
    float upY = max(up.y, 0.0);
    vec3 horizonX = volumetricCloudSkyFullHorizonBase(normalize(vec3(1.0, upY, 0.0)));
    vec3 horizonZ = volumetricCloudSkyFullHorizonBase(normalize(vec3(0.0, upY, 1.0)));
    vec3 ambient = volumetricCloudSkyRadiance(up) * 0.55 + 0.225 * (horizonX + horizonZ);
    return ambient * VPT_VOLUMETRIC_CLOUD_AMBIENT_STRENGTH;
}

vec3 sampleCloudAmbientIrradianceUp() {
    return sampleCloudAmbientIrradiance(vec3(0.0, 1.0, 0.0));
}

// Light bounced up from the ground into the cloud base, approximated from the sky below and at the horizon. It
// grows as the light gets lower (long sunsets light the undersides) and inside denser cloud.
vec3 volumetricCloudGroundToCloudTransfertIsoScatter(vec3 up, float transmittance, float powderEffect) {
    vec3 bounce = mix(volumetricCloudSkyFullBelowHorizonBase(-up), volumetricCloudSkyFullHorizonBase(vec3(up.x, 0.02, up.z)),
                      0.35);
    float lowLight = 1.0 - dot(volumetricCloudPrimaryLightDir(), vec3(0.0, 1.0, 0.0));
    lowLight *= 1.0 + lowLight;
    return bounce * powderEffect * clamp(lowLight, 0.0, 1.0) * mix(vec3(1.0), vec3(1.35), cloudSaturate(1.0 - transmittance));
}

float volumetricCloudLuminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Sky-cube colour near the horizon, with the direction held at the minimum view cosine (as sky_cube.frag does),
// rain-filtered.
vec3 volumetricCloudSkyFullHorizonBase(vec3 rayDir) {
    float aboveHorizon = smoothstep(-0.05, 0.02, rayDir.y);
    vec3 lifted = vec3(rayDir.x, max(rayDir.y, VPT_ATMOSPHERE_MIN_VIEW_COS), rayDir.z);
    lifted = normalize(mix(vec3(rayDir.x, VPT_ATMOSPHERE_MIN_VIEW_COS, rayDir.z), lifted, aboveHorizon));
    return volumetricCloudRainFilter(texture(skyFull, lifted).rgb, 0.95);
}

// Same for just below the horizon (a slightly darker band), rain-filtered darker still.
vec3 volumetricCloudSkyFullBelowHorizonBase(vec3 rayDir) {
    const float belowHorizonCos = VPT_ATMOSPHERE_MIN_VIEW_COS - 0.035;
    float aboveBand = smoothstep(-0.06, 0.03, rayDir.y);
    vec3 lifted = vec3(rayDir.x, max(rayDir.y, belowHorizonCos), rayDir.z);
    lifted = normalize(mix(vec3(rayDir.x, belowHorizonCos, rayDir.z), lifted, aboveBand));
    return volumetricCloudRainFilter(texture(skyFull, lifted).rgb, 0.70);
}

// How much a distant cloud should dissolve into the horizon: only far away (24-90 km), only when seen nearly
// edge-on, and only for its lower parts.
float volumetricCloudHorizonFade(vec3 rayDir, vec3 cloudWorldPos, float cloudDistance) {
    vec3 planetPos = cloudPlanetPos(cloudWorldPos);
    float grazing = 1.0 - smoothstep(0.015, 0.070, dot(rayDir, cloudSafeNormalize(planetPos, vec3(0.0, 1.0, 0.0))));
    float far = smoothstep(24000.0, 90000.0, cloudDistance);
    float low = 1.0 - smoothstep(0.22, 0.65, cloudHeight01(planetPos));
    return clamp(grazing * far * low, 0.0, 1.0);
}

// Near the horizon the cloud lighting takes on the horizon's colour so clouds do not stand out from the sky there.
float volumetricCloudHorizonMatch(vec3 rayDir) {
    return 1.0 - smoothstep(0.035, 0.16, rayDir.y);
}

// At and below the horizon the result fades to the below-horizon sky.
float volumetricCloudHorizonMissMix(vec3 rayDir) {
    return 1.0 - smoothstep(0.0, 0.08, rayDir.y);
}

// Recolours light to the horizon's hue at the same luminance, by strength.
vec3 vclTintToHorizon(vec3 light, vec3 horizon, float strength) {
    float lightLuma = volumetricCloudLuminance(max(light, vec3(0.0)));
    float horizonLuma = max(volumetricCloudLuminance(max(horizon, vec3(0.0))), 1e-4);
    return mix(light, max(horizon, vec3(0.0)) * (lightLuma / horizonLuma), strength);
}

// Energy-conserving in-scattering over one step (the analytic integral over the step of light * sigma_s * T).
vec3 vclStepScatter(vec3 incident, float sigmaScatter, float sigmaExtinction, float transmittance, float stepTransmittance) {
    vec3 lit = incident * vec3(sigmaScatter);
    return vec3(transmittance) * (lit - lit * stepTransmittance) / sigmaExtinction;
}

void vclBlendBelowHorizon(inout VolumetricCloudResult result, vec3 rayDir) {
    float below = volumetricCloudHorizonMissMix(rayDir);
    if (below > 1e-4) {
        result.color = mix(result.color, volumetricCloudSkyFullBelowHorizonBase(rayDir), below);
        result.transmittance = mix(result.transmittance, 1.0, below);
    }
}

// Light transport towards the dominant light from a sample, with the optional soft-shadow taps; (1, 1) when cloud
// self-shadowing is off (light near the horizon or strength 0).
vec2 vclSunTransport(vec3 samplePos, vec3 lightDir, int lightStepCount, float shadowWeight, float softness) {
    if (shadowWeight <= 1e-4) { return vec2(1.0); }

    vec3 start = samplePos + lightDir * 5.0;
    float distanceInCloud = volumetricCloudSegmentLength(start, lightDir);
    vec2 transport = marchCloudLightTransport(start, lightDir, distanceInCloud, lightStepCount, 0.175);
    if (softness > 1e-4) {
        vec3 offset = vclShadowOffsetDirection(lightDir) * (clamp(distanceInCloud * 0.04, 40.0, 450.0) * softness);
        int tapSteps = max(lightStepCount / 2, 1);

        vec3 startA = start + offset;
        float distanceA = volumetricCloudSegmentLength(startA, lightDir);
        vec2 sideA = distanceA <= 1e-3 ? vec2(1.0) : marchCloudLightTransport(startA, lightDir, distanceA, tapSteps, 0.175);
        vec3 startB = start - offset;
        float distanceB = volumetricCloudSegmentLength(startB, lightDir);
        vec2 sideB = distanceB <= 1e-3 ? vec2(1.0) : marchCloudLightTransport(startB, lightDir, distanceB, tapSteps, 0.175);

        transport = mix(transport, transport * 0.50 + (sideA + sideB) * 0.25, softness);
    }
    return mix(vec2(1.0), transport, shadowWeight);
}

// The cloud march. skyBackgroundRadiance is what the ray would see without clouds; the step counts set the
// quality (the primary view uses the VPT_VOLUMETRIC_CLOUD_* budgets, reflections the cheaper indirect ones).
VolumetricCloudResult applyVolumetricCloudBudgeted(
    vec3 rayOrigin, vec3 rayDir, vec3 skyBackgroundRadiance, int viewStepCount, int lightStepCount, int ambientStepCount) {
    VolumetricCloudResult result;
    result.color = skyBackgroundRadiance;
    result.transmittance = 1.0;
    result.hit = 0.0;
    if (VPT_CLOUD_MODE != 2u) { return result; }

    float tEnter, tExit;
    if (!intersectCloudLayer(rayOrigin, rayDir, tEnter, tExit)) { return result; }
    float pathLength = tExit - tEnter;
    if (pathLength <= 1e-3) { return result; }

    // Per-ray constants.
    vec3 lightDir = volumetricCloudPrimaryLightDir();
    vec3 lightRadiance = volumetricCloudPrimaryLightRadiance();
    vec3 horizonBase = volumetricCloudSkyFullHorizonBase(rayDir);
    vec3 belowHorizonBase = volumetricCloudSkyFullBelowHorizonBase(rayDir);
    float horizonMatch = volumetricCloudHorizonMatch(rayDir);
    float viewToLight = clamp(dot(rayDir, lightDir), -1.0, 1.0);
    vec2 phases = getParticipatingMediaPhase(dualLobePhase(0.5, -0.5, 0.5, -viewToLight));
    float rainBlend = volumetricCloudRainBlend();
    // A low light travels further through cloud, so its multiple scattering is allowed to reach deeper.
    float lowLightBoost = 1.0 + cloudSaturate(1.0 - lightDir.y * 2.0);
    float shadowWeight = smoothstep(0.06, 0.24, max(lightDir.y, 0.0)) * clamp(VPT_VOLUMETRIC_CLOUD_SHADOW_STRENGTH, 0.0, 1.0);
    float softness = clamp(VPT_VOLUMETRIC_CLOUD_SHADOW_SOFTNESS, 0.0, 1.0);

    viewStepCount = max(viewStepCount, 1);
    lightStepCount = max(lightStepCount, 1);
    ambientStepCount = max(ambientStepCount, 1);
    // Light rays are expensive: reuse each one for a few view steps unless the cloud changes noticeably.
    int sunReuse = clamp(viewStepCount / 32, 2, 6);
    int ambientReuse = clamp(viewStepCount / 24, 2, 8);
    float stepLength = pathLength / float(viewStepCount);
    // Per-pixel, per-frame start offset within a step; the temporal pass averages the banding away.
    float jitter = cloudHash12(vec2(gl_LaunchIDEXT.xy) + vec2(float(worldUBO.seed & 255u), 17.0));

    float transmittance = 1.0;
    vec3 scattering = vec3(0.0);
    // Transmittance-weighted centroid of the cloud along the ray, for the aerial perspective and horizon fade.
    vec3 centroidSum = vec3(0.0);
    float centroidWeight = 0.0;
    vec2 sunTransport = vec2(1.0);
    vec2 ambientTransport = vec2(1.0);
    int sunAge = 0;
    int ambientAge = 0;
    float lastDensity = 0.0;
    bool hadLastDensity = false;

    for (int stepIndex = 0; stepIndex < viewStepCount; ++stepIndex) {
        vec3 samplePos = rayOrigin + rayDir * min(tEnter + (float(stepIndex) + jitter) * stepLength, tExit);

        VolumetricCloudSampleContext context = sampleCloudContext(samplePos);
        int stride = cloudSkipStepCount(sampleCloudMacroOccupancyFromContext(context), result.hit < 0.5);
        if (stride > 1) {
            stepIndex += min(stride - 1, viewStepCount - 1 - stepIndex);
            continue;
        }
        float density = sampleCloudDensityFromContext(context);
        if (density <= 1e-5) { continue; }

        result.hit = 1.0;
        centroidSum += samplePos * transmittance;
        centroidWeight += transmittance;

        float opticalDepth = density * stepLength;
        // Beer's law with a long tail, so very dense steps still let a little through (keeps cores from going black).
        float stepTransmittance = max(exp(-opticalDepth), exp(-opticalDepth * 0.25) * 0.70);
        vec3 planetPos = cloudPlanetPos(samplePos);
        vec3 up = cloudSafeNormalize(planetPos, vec3(0.0, 1.0, 0.0));

        // Refresh the cached light transport when it is due, or when the density jumps (entering a new cloud).
        bool refreshSun = sunAge <= 0;
        bool refreshAmbient = ambientAge <= 0;
        if (hadLastDensity) {
            float change = abs(density - lastDensity);
            refreshSun = refreshSun || change > 0.06 || context.coverage < 0.12;
            refreshAmbient = refreshAmbient || change > 0.08;
        }
        if (refreshSun) {
            sunTransport = vclSunTransport(samplePos, lightDir, lightStepCount, shadowWeight, softness);
            sunAge = sunReuse;
        } else {
            sunAge -= 1;
        }
        if (refreshAmbient) {
            vec3 skyward = samplePos + up * 5.0;
            ambientTransport =
                marchCloudLightTransport(skyward, up, volumetricCloudSegmentLength(skyward, up), ambientStepCount, 0.50);
            ambientAge = ambientReuse;
        } else {
            ambientAge -= 1;
        }

        float radius = clamp(length(planetPos), VPT_ATMOSPHERE_RG, VPT_ATMOSPHERE_RT);
        vec3 sunlight = sampleCloudAtmosphereTransmittance(radius, dot(up, lightDir)) * lightRadiance;
        float depthTerm =
            pow(clamp(density * 8.0, 0.0, 1.0), cloudRemapClamped(context.layerHeight01, 0.3, 0.85, 0.5, 2.0)) + 0.05;
        float heightTerm = pow(cloudRemapClamped(context.layerHeight01, 0.07, 0.22, 0.1, 1.0), 0.8);
        float powder = mix(1.0, powderEffectNew(depthTerm, heightTerm, viewToLight),
                           clamp(VPT_VOLUMETRIC_CLOUD_POWDER_STRENGTH, 0.0, 1.0));

        vec3 skyAmbient = sampleCloudAmbientIrradiance(up);
        vec3 zenithAmbient = sampleCloudAmbientIrradianceUp();
        vec3 groundBounce = volumetricCloudGroundToCloudTransfertIsoScatter(up, transmittance, powder);
        vec3 distantAmbient = mix(belowHorizonBase, zenithAmbient, 0.35);
        float extinction = max(density, 1e-5);
        // The multiple-scattering octave sees reduced extinction.
        float extinctionMulti = max(extinction * (0.175 / lowLightBoost), 1e-5);
        float matchStrength = horizonMatch * mix(0.9, 0.30, rainBlend);

        // Multiple-scattering octave: the light only, with the softened phase and the y transport.
        vec3 incidentMulti = sunTransport.y * sunlight * phases.y * powder;
        if (horizonMatch > 1e-4) { incidentMulti = vclTintToHorizon(incidentMulti, horizonBase, matchStrength); }
        scattering += vclStepScatter(incidentMulti, density, extinctionMulti, transmittance, stepTransmittance);

        // Single-scattering octave: the light plus all ambient terms.
        vec3 incident = sunTransport.x * sunlight * phases.x * powder;
        incident += ambientTransport.x * skyAmbient * powder;
        incident += groundBounce;
        incident += distantAmbient * 0.35 * VPT_VOLUMETRIC_CLOUD_AMBIENT_STRENGTH;
        if (horizonMatch > 1e-4) { incident = vclTintToHorizon(incident, horizonBase, matchStrength); }
        scattering += vclStepScatter(incident, density, extinction, transmittance, stepTransmittance);
        transmittance *= stepTransmittance;

        // Once less than 1% of the background shows through, nothing behind can be told apart any more; going on
        // would only pay for light rays through the thickest part of the cloud.
        if (transmittance <= 0.01) {
            transmittance = 0.0;
            break;
        }
        lastDensity = density;
        hadLastDensity = true;
    }

    result.transmittance = transmittance;
    if (centroidWeight <= 1e-4) {
        // No cloud met (only strided through): background plus whatever little was scattered.
        result.color = skyBackgroundRadiance * transmittance + scattering;
        vclBlendBelowHorizon(result, rayDir);
        return result;
    }

    // Aerial perspective: the air between the camera and the cloud centroid dims the cloud light and adds its own
    // in-scattering where the cloud blocks the background.
    vec3 cloudCentre = centroidSum / centroidWeight;
    float cloudDistance = max(dot(cloudCentre - rayOrigin, rayDir), 0.0);
    VolumetricCloudAtmosphereSegmentResult air = integrateAtmosphereSegment(rayOrigin, rayDir, cloudDistance);
    scattering = scattering * air.transmittance + air.scatteredLight * (1.0 - transmittance);

    // Far, low, grazing clouds dissolve into the horizon colour.
    float fade = volumetricCloudHorizonFade(rayDir, cloudCentre, cloudDistance);
    horizonBase = max(skyBackgroundRadiance, horizonBase);
    if (fade > 1e-4) {
        scattering = vclTintToHorizon(scattering, horizonBase, fade * mix(0.85, 0.35, rainBlend));
        transmittance = mix(transmittance, 1.0, fade);
    }
    scattering *= (1.0 - fade);

    result.transmittance = transmittance;
    result.color = horizonBase * transmittance + scattering;
    vclBlendBelowHorizon(result, rayDir);
    return result;
}

VolumetricCloudResult applyVolumetricCloud(vec3 rayOrigin, vec3 rayDir, vec3 skyBackgroundRadiance) {
    return applyVolumetricCloudBudgeted(rayOrigin, rayDir, skyBackgroundRadiance,
                                        VPT_VOLUMETRIC_CLOUD_VIEW_STEPS,
                                        VPT_VOLUMETRIC_CLOUD_LIGHT_STEPS,
                                        VPT_VOLUMETRIC_CLOUD_AMBIENT_STEPS);
}

#endif
