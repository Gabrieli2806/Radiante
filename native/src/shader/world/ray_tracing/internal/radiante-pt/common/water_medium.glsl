#ifndef VPT_WATER_MEDIUM_GLSL
#define VPT_WATER_MEDIUM_GLSL

#include "hooks/water.glsl"

// Water as a participating medium, the way Bedrock RTX treats it: along a path through the water light is absorbed and
// scattered per colour channel, so shallows stay clear and deep water turns blue or green. The coefficients come from a
// converted Bedrock pack's biome fog, or else from the biome's vanilla water colour (skyUBO.waterExtinction.w is 1 once
// either is set).
//
// Needs skyUBO, skyFull and the volumetric cloud helpers (for the sun or moon light) to be declared first.

#ifndef VPT_WATER_SCATTERING
#    define VPT_WATER_SCATTERING 2.0
#endif
// How strongly the light scattered in the water lights what is under it (the stretches light crosses on its way to
// a surface, not the camera's view): Bedrock RTX's sunken walls glow with it on every side.
#ifndef VPT_WATER_WAVE_STRENGTH
#    define VPT_WATER_WAVE_STRENGTH 0.35
#endif

// How thick the water looks through its surface from above (VPT_WATER_DENSITY is the same from inside it): Bedrock
// RTX shows the sea floor teal and darkened from above, while under water the same water reads clear.
#ifndef VPT_WATER_DENSITY_ABOVE
#    define VPT_WATER_DENSITY_ABOVE 0.4
#endif
#ifndef VPT_WATER_AMBIENT
#    define VPT_WATER_AMBIENT 1.0
#endif
#ifndef VPT_WATER_DENSITY
#    define VPT_WATER_DENSITY 0.1
#endif

// How thick the water is to look through, against its coefficients: Bedrock RTX's water is clear up close and
// only fogs out far away, while the light through it still takes on its colour. Only the camera's own stretch of
// water is thinned; the light's paths (bounces, and the sun in shadow.rahit) cross it at full strength.
vec3 waterMediumExtinction(float density) {
    vec3 extinction = max(skyUBO.waterExtinction.rgb, vec3(0.0));
    hookWaterAbsorption(extinction); // a pack's water colour (hooks/water.glsl); nothing in radiante-pt
    return extinction * max(density, 0.0);
}

// A stretch of water a ray crossed after leaving a surface (the bounce it is on): how thick it is, and how much the
// light scattered in it adds. A ray seen straight through the surface from above the water is the camera's view,
// thinned (VPT_WATER_DENSITY) and not brightened: Bedrock RTX's water is clear and dark from above, and only the
// light filling it under water glows (VPT_WATER_AMBIENT, when the camera is in it).
float waterMediumBounceDensity(uint bounce) {
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    return 1.0; // Bedrock applies the medium's extinction (observed) without an above-water density multiplier.
#else
    return bounce <= 1u && skyUBO.cameraSubmersionType != 1 ? VPT_WATER_DENSITY_ABOVE : 1.0;
#endif
}

float waterMediumBounceAmbient(uint bounce) {
    if (skyUBO.cameraSubmersionType == 1) { return VPT_WATER_AMBIENT; }
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // Bedrock RTX's water glows with the light it scatters seen from above too: a deep pool is a lit blue down to
    // its walls, where without it they were black behind the clear surface.
    return VPT_WATER_AMBIENT;
#endif
    // Seen from above, the water shows what is under it and nothing of its own: the scattered light is lit by the
    // open sky whether or not the water is (an indoor pool glowed sky blue), and Bedrock's water is clear.
    return bounce <= 1u ? 0.0 : 1.0;
}

bool waterMediumActive() {
    return skyUBO.waterExtinction.w > 0.5;
}

// How far under the surface the camera is, in blocks (waterExtinction.w is 1 plus that; 0 while under no water).
float waterCameraDepth() {
    return skyUBO.cameraSubmersionType == 1 ? clamp(skyUBO.waterExtinction.w - 1.0, 0.0, 64.0) : 0.0;
}

// How small the body of water around the camera is, 0..1: 1 in a pool a block or two across and deep, 0 in a lake,
// the sea or out of water. Its size is the geometric mean of the water column's height and how far the water runs
// across at eye level (skyUBO.waterBodyDepth and waterBodyWidth, from WaterBody on the Java side), so a narrow deep
// well and a wide shallow puddle both count as small. A 3-deep pond five across is about 0.95, a river a half, the
// ocean 0. The biome's water fog is the sea's; in a pool little of it has room to gather, so radiante-pt clears it.
#ifndef VPT_WATER_BODY_SMALL
#    define VPT_WATER_BODY_SMALL 2.0
#endif
#ifndef VPT_WATER_BODY_LARGE
#    define VPT_WATER_BODY_LARGE 16.0
#endif
float waterBodyShallowness() {
    if (skyUBO.cameraSubmersionType != 1 || skyUBO.waterBodyDepth <= 0.0) { return 0.0; }
    float size = sqrt(max(skyUBO.waterBodyDepth, 0.0) * max(skyUBO.waterBodyWidth, 1.0));
    return 1.0 - smoothstep(VPT_WATER_BODY_SMALL, VPT_WATER_BODY_LARGE, size);
}

// Light the water scatters towards the viewer: the sky above and the sun or moon, both taken as they reach the surface.
// How deep the scattering happens is not known here, so light that has already crossed some water is not dimmed for it.
vec3 waterMediumIncomingLight() {
    vec3 zenith = texture(skyFull, vec3(0.0, 1.0, 0.0)).rgb;
    vec3 horizon = texture(skyFull, normalize(vec3(0.7, 0.35, 0.6))).rgb;
    // Isotropic phase: the sky fills about half the sphere, the sun is one direction of it.
    vec3 sky = (zenith + horizon) * 0.25;
    vec3 celestial = volumetricCloudPrimaryLightRadiance() * 0.0795775 * (1.0 - skyUBO.rainGradient * 0.65);
    // Under water, the light reaching the water around the camera has crossed the water above it: near the surface
    // the water glows as bright as daylight, and it darkens the deeper the camera goes, as in Bedrock RTX. Before,
    // the water at the bottom of a sea glowed as brightly as at its top.
    vec3 overhead = exp(-max(skyUBO.waterExtinction.rgb, vec3(0.0)) * waterCameraDepth());
    return max(sky + celestial, vec3(0.0)) * overhead;
}

// The colour the scattered light takes: white light after this many blocks of the water. Scattering alone (its
// albedo, bluest of all) left Bedrock packs' water a deep navy, where Bedrock RTX shows it turquoise: most of the
// light the water glows with has crossed some of it first, off the bottom and the walls.
const float WATER_MEDIUM_HUE_DEPTH = 8.0;

// What a stretch of water of the given length lets through, and the light it scatters into it on the way; density
// scales the water (VPT_WATER_DENSITY for the camera's view, 1 for light).
#ifndef VPT_MEDIUM_DISTANCE_CLAMP
#    define VPT_MEDIUM_DISTANCE_CLAMP 0
#endif

// As below without the distance clamp: water with no far side (a ray that left the loaded world still under water)
// lets nothing through, whatever the clamp makes of a finite stretch.
void waterMediumSegmentUnclamped(float distance, float density, out vec3 transmittance, out vec3 inScatter) {
    vec3 sigmaT = waterMediumExtinction(density);
    transmittance = exp(-sigmaT * max(distance, 0.0));
    const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);
    vec3 hue = exp(-max(skyUBO.waterExtinction.rgb, vec3(0.0)) * WATER_MEDIUM_HUE_DEPTH);
    hue /= max(dot(hue, LUMA), 1e-4);
    float albedo = clamp(dot(skyUBO.waterAlbedo.rgb, LUMA) * max(VPT_WATER_SCATTERING, 0.0), 0.0, 1.0);
    float light = dot(waterMediumIncomingLight(), LUMA);
    inScatter = hue * albedo * light * (vec3(1.0) - transmittance);
}

void waterMediumSegment(float distance, float density, out vec3 transmittance, out vec3 inScatter) {
#if VPT_MEDIUM_DISTANCE_CLAMP != 0
    // Bedrock compatibility - observed behaviour. The distance a
    // ray is attenuated over inside a medium stops counting at 50 blocks.
    distance = min(distance, 50.0);
#endif
    waterMediumSegmentUnclamped(distance, density, transmittance, inScatter);
}

#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
// How much brighter Bedrock RTX's water glows than single scattering of the sun and sky with an even phase would
// make it, with the pack's coefficients as they stand (matched against Bedrock RTX side by side: Vanilla RTX, plains,
// noon, pools seen from above and from under water).
#    ifndef VPT_BEDROCK_WATER_SCATTER_GAIN
#        define VPT_BEDROCK_WATER_SCATTER_GAIN 2.4
#    endif
const float BEDROCK_WATER_SKY_SHARE = 0.5;
const float BEDROCK_WATER_SUN_SHARE = 1.6;

// Bedrock RTX's water, per colour channel: the pack's absorption and scattering as they stand, the light scattered
// along the stretch coming down from the surface through the water above it. Side by side with Bedrock the light
// glows blue (the pack scatters blue most and lets it through furthest), where the luminance-and-hue rule above made
// it teal-green and, under water, far darker.
// origin and direction are the stretch's start and direction relative to the camera. How deep the start lies is
// known while the camera is in the same water (its depth under the surface); from above, a stretch starts at the
// surface the view came in through, or on what lies under it, and is taken as starting at the top.
void waterMediumSegmentBedrock(float distance, float density, vec3 origin, vec3 direction, out vec3 transmittance,
                               out vec3 inScatter) {
    vec3 sigmaLight = max(skyUBO.waterExtinction.rgb, vec3(0.0));
    vec3 sigmaT = sigmaLight * max(density, 0.0);
    transmittance = exp(-sigmaT * max(distance, 0.0));
    float startDepth = skyUBO.cameraSubmersionType == 1 ? max(waterCameraDepth() - origin.y, 0.0) : 0.0;
    // Along the stretch the light's own way down through the water grows by how steeply the stretch goes down.
    float descent = -normalize(direction).y;
    vec3 sigmaCombined = max(sigmaT + sigmaLight * descent, sigmaT * 0.05 + 1e-4);
    float span = min(max(distance, 0.0), 4096.0);
    vec3 along = (vec3(1.0) - exp(-sigmaCombined * span)) / sigmaCombined;
    vec3 sigmaS = sigmaLight * clamp(skyUBO.waterAlbedo.rgb, vec3(0.0), vec3(1.0));
    vec3 zenith = texture(skyFull, vec3(0.0, 1.0, 0.0)).rgb;
    vec3 horizon = texture(skyFull, normalize(vec3(0.7, 0.35, 0.6))).rgb;
    // The sky's light counts for less than an even phase gives it and the sun's for more: the sky's comes in at a
    // slant and much of it is turned back at the surface, and Bedrock's water glows a paler blue than the sky alone
    // would make it (matched side by side, as the gain).
    vec3 light = (zenith + horizon) * (0.25 * BEDROCK_WATER_SKY_SHARE) +
                 volumetricCloudPrimaryLightRadiance() * (0.0795775 * BEDROCK_WATER_SUN_SHARE) *
                     (1.0 - skyUBO.rainGradient * 0.65);
    inScatter = VPT_BEDROCK_WATER_SCATTER_GAIN * sigmaS * max(light, vec3(0.0)) * exp(-sigmaLight * startDepth) *
                along;
}
#endif

#endif
