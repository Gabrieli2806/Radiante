#ifndef VPT_PT_VOLUMETRIC_GLSL
#define VPT_PT_VOLUMETRIC_GLSL

// The per-pixel volumetric fog march: the air medium, light shafts, the biome's fog and the water god rays, lit by
// shadow rays towards the sun and up to the sky. Moved verbatim out of world.rgen.

#include "common/biome_haze.glsl"

float volumetricLightLuminance(vec3 value) {
    return dot(value, vec3(0.2126, 0.7152, 0.0722));
}

float henyeyGreenstein(float cosTheta, float g) {
    float gg = g * g;
    float denom = max(1.0 + gg - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - gg) * INV_4_PI / (denom * sqrt(denom));
}

float volumetricMediumMaxDistance() {
    return clamp(VPT_VOLUMETRIC_LIGHT_MAX_DISTANCE, 32.0, 320.0);
}

// How thick the air is: a little hazier with the sun low, as in the morning and evening, and the same at every
// distance. It used to thin out near the camera and thicken past the vanilla fog start, and the step between the two
// read as a wall of fog at a fixed distance.
float volumetricAirDensity() {
    float sunHeight = abs(celestialSunDirection().y);
    return mix(1.0, 0.7, smoothstep(0.05, 0.35, sunHeight));
}

vec3 applySampledLightChroma(vec3 lightRadiance, vec3 lightDir) {
    // Bedrock's sunlight table already gives the light its colour.
    if (bedrockAtmosphereActive()) { return lightRadiance; }
    vec3 sampledRadiance = max(texture(skyFull, lightDir).rgb, vec3(0.0));
    float sampledLum = volumetricLightLuminance(sampledRadiance);
    if (!finiteFloat(sampledLum) || sampledLum <= 1e-6) { return lightRadiance; }
    // The sky's hue at the sun, as a factor: the light keeps whatever colour glass on its way gave it. Replacing its
    // colour outright turned a shaft through yellow glass the sky's blue.
    return lightRadiance * mix(vec3(1.0), sampledRadiance / sampledLum, 0.65);
}

vec3 sampleVolumetricDirectionalLight(vec3 position, vec3 lightDir, bool insideBoat) {
    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = insideBoat ? 1u : 0u;
    shadowRay.pad0 = 0u;

    // Vanilla clouds are left out: their mesh only covers the few chunks around the camera, and the shadow it cast
    // into the fog ended in a straight line across the sky where the mesh did.
    uint shadowMask = WORLD_MASK | PLAYER_MASK;
    vec3 origin = position + lightDir * 0.02;
    traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS, shadowMask, 0, 0, 0, origin, 0.001, lightDir, 1000.0, 1);
    return applySampledLightChroma(shadowRay.radiance, lightDir) * (1.0 - skyUBO.rainGradient);
}

// 1 when the sky is in sight straight up from here, 0 under a roof.
float volumetricSkyVisibility(vec3 position, bool insideBoat) {
    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = insideBoat ? 1u : 0u;
    // The miss shader answers a block light query with plain white: sky in sight or not, whatever the sun does.
    shadowRay.pad0 = BLOCK_LIGHT_QUERY;
    traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS, WORLD_MASK, 0, 0, 0, position + vec3(0.0, 0.02, 0.0), 0.001,
                vec3(0.0, 1.0, 0.0), 512.0, 1);
    return clamp(volumetricLightLuminance(shadowRay.radiance), 0.0, 1.0);
}

void volumetricAirMedium(float cosTheta, out vec3 sigmaS, out vec3 sigmaT) {
    // A soft forward lobe, as in the sky (sky_cube.frag): a sharp one lit the fog in front of the sun into a white
    // halo around it.
    float mieG = clamp(min(VPT_ATMOSPHERE_MIE_G, 0.45), -0.2, 0.85);
    float miePhase = min(henyeyGreenstein(cosTheta, mieG), 1.2);
    float rPhase = 3.0 * INV_4_PI * 0.25 * (1.0 + cosTheta * cosTheta);
    float fogPhase = mix(INV_4_PI, miePhase, 0.75);
    // Vanilla's fog distance is no measure of the air: maps and mods pull it in to a few dozen blocks, and it fogged
    // everything white. The air is the atmosphere's own (and the biome's fog, marched with it), as in Bedrock RTX.
    float fogDensity = 0.0;

    vec3 betaR = VPT_ATMOSPHERE_BETA_R * 18.0;
    vec3 betaM = VPT_ATMOSPHERE_BETA_M * mix(18.0, 54.0, skyUBO.rainGradient);
    sigmaS = betaR * rPhase + betaM * miePhase + vec3(fogDensity * fogPhase);
    sigmaT = betaR + betaM * 1.15 + vec3(fogDensity);
}

#ifndef VPT_LIGHT_SHAFTS
#    define VPT_LIGHT_SHAFTS 0.015
#endif
const float LIGHT_SHAFT_REACH = 48.0;

#ifndef VPT_WATER_GOD_RAYS
#    define VPT_WATER_GOD_RAYS 0.12
#endif

// Shafts of sunlight in the water around the camera, as Bedrock RTX shows them: the sun (or moon) traced from a few
// points along the camera's stretch of water, through the surface and its caustics (shadow.rahit), so the waves and
// anything floating break the light into rays. VPT_WATER_GOD_RAYS is how much of that light the water scatters
// towards the eye.
vec3 waterGodRays(vec3 origin, vec3 rayDir, float distance) {
    float strength = max(VPT_WATER_GOD_RAYS, 0.0);
    if (strength <= 0.0 || distance <= 0.05) { return vec3(0.0); }
    const int STEPS = 8;
    float reach = min(distance, 48.0);
    vec3 lightDir = celestialSunDirection();
    if (lightDir.y < 0.0) { lightDir = -lightDir; }
    // Mostly forward: the shafts show best looking towards the light, as under real water.
    float phase = mix(INV_4_PI, min(henyeyGreenstein(dot(rayDir, lightDir), 0.6), 2.0), 0.7);
    vec3 sigmaT = waterMediumExtinction(VPT_WATER_DENSITY);
    uint seed = xxhash32(uvec3(uint(gl_LaunchIDEXT.x), uint(gl_LaunchIDEXT.y), worldUBO.seed ^ 0x3C6EF372u));
    float jitter = rand(seed);
    float stepLength = reach / float(STEPS);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < STEPS; ++i) {
        float t = (float(i) + jitter) * stepLength;
        vec3 light = sampleVolumetricDirectionalLight(origin + rayDir * t, lightDir, false);
        sum += exp(-sigmaT * t) * light;
    }
    return sum * stepLength * phase * strength;
}

vec3 segmentIntegral(vec3 sigmaT, vec3 segmentTransmittance, float stepLength) {
    return vec3(sigmaT.r > 1e-6 ? (1.0 - segmentTransmittance.r) / sigmaT.r : stepLength,
                sigmaT.g > 1e-6 ? (1.0 - segmentTransmittance.g) / sigmaT.g : stepLength,
                sigmaT.b > 1e-6 ? (1.0 - segmentTransmittance.b) / sigmaT.b : stepLength);
}

vec3 softClampVolumetricLight(vec3 value) {
    if (!finiteVec3(value)) { return vec3(0.0); }

    float limit = max(VPT_VOLUMETRIC_LIGHT_LUMINANCE_LIMIT, 1e-4);
    float lum = volumetricLightLuminance(max(value, vec3(0.0)));
    if (lum <= 1e-6) { return vec3(0.0); }

    float knee = limit * 0.25;
    if (lum <= knee) { return value; }

    float range = max(limit - knee, 1e-4);
    float compressedLum = min(knee + range * (1.0 - exp(-(lum - knee) / range)), limit);
    return value * (compressedLum / lum);
}

// Where the i-th of n steps starts, as a share of the marched distance: the steps are packed near the camera, where
// shafts are sharp and close, and spread out further away, and always reach the end of the march however long it
// is. A fixed step size used to run out of steps a few dozen blocks out, and past that point the air stopped
// scattering light at all, which showed as a cut across the terrain.
float volumetricStepFraction(float i, float n) {
    float x = clamp(i / max(n, 1.0), 0.0, 1.0);
    return x * x;
}

// Per-biome fog as a participating medium, the way Bedrock RTX does it: the biome's density both absorbs and
// scatters, lit by the sun or moon through shadow rays (so trees and terrain cast light shafts into it) and by the sky
// all around. Only in the overworld; skyUBO.biomeFog.a is already zero underground and when the option is off.
float biomeFogDensity() {
    return worldUBO.skyType == 1 ? max(skyUBO.biomeFog.a, 0.0) : 0.0;
}

// How far along this ray the biome fog reaches: the hit itself, or for the sky a distance that is long at the horizon
// and short towards the zenith, so looking up stays clear.
/** The darkest sky the albedo guide still divides by; below it the guide itself goes dark. See main. */
const float SKY_GUIDE_MIN_LEVEL = 0.02;

float biomeFogReach(vec3 rayDir, bool hitSky, float hitDistance) {
    return hitSky ? biomeFogSkyReach(rayDir) : min(hitDistance, BIOME_HAZE_MAX_DISTANCE);
}

vec3 biomeFogAmbient(vec3 rayDir) {
    vec3 horizon = texture(skyFull, normalize(vec3(rayDir.x, 0.12, rayDir.z))).rgb;
    vec3 zenith = texture(skyFull, vec3(0.0, 1.0, 0.0)).rgb;
    return max((horizon + zenith) * 0.5, vec3(0.0));
}

void integrateVolumetricFog(vec3 origin,
                            vec3 rayDir,
                            float endDistance,
                            float biomeReach,
                            bool insideBoat,
                            out vec3 transmittance,
                            out vec3 radiance) {
    transmittance = vec3(1.0);
    radiance = vec3(0.0);
    if (!finiteFloat(endDistance) || endDistance <= 0.05) { return; }

    // Shadow rays are traced out to the volumetric distance; beyond it the medium carries on unshadowed to whatever
    // the ray hits, so far terrain gets the same haze as near terrain instead of a hard edge where the march ends.
    float marchEnd = min(endDistance, volumetricMediumMaxDistance());

    vec3 lightDir = celestialSunDirection();
    if (lightDir.y < 0.0) { lightDir = -lightDir; }

    float cosTheta = clamp(dot(rayDir, lightDir), -1.0, 1.0);
    vec3 sigmaS;
    vec3 sigmaT;
    volumetricAirMedium(cosTheta, sigmaS, sigmaT);
    float airDensity = volumetricAirDensity();
    float scatteringStrength = max(VPT_VOLUMETRIC_LIGHT_SCATTERING_STRENGTH, 0.0);
    // The Volumetric Fog Strength option thickens or thins the air as a whole, its glow and its haze together.
    float fogStrength = max(skyUBO.fogControls.z, 0.0);
    vec3 airSigmaS = sigmaS * airDensity * scatteringStrength * fogStrength;
    vec3 airSigmaT = sigmaT * airDensity * 0.45 * fogStrength;

    float biomeDensity = biomeFogDensity();
    vec3 biomeAlbedo = clamp(skyUBO.biomeFog.rgb, vec3(0.0), vec3(1.5));
    // How strongly the sun or moon lights the biome fog against the sky's light from all around.
    float biomeSunScatter = max(VPT_BIOME_FOG_SCATTERING, 0.0);
    vec3 biomeChroma = biomeFogChroma();
    float biomePhase = mix(INV_4_PI, min(henyeyGreenstein(cosTheta, VPT_BIOME_FOG_FORWARD), 4.0), 0.6);
    float shaftPhase = mix(INV_4_PI, min(henyeyGreenstein(cosTheta, 0.5), 2.0), 0.5);
    // The sky lights the fog from every side, which is what keeps it from going black in the shade the way a fog
    // lit by the sun alone does; Bedrock RTX's fog keeps the sky's colour in shadow for the same reason.
    vec3 skyAmbient = biomeFogAmbient(rayDir);
    // How open to the sky the air along this ray is, from three looks straight up: the sky's light fills the fog
    // outdoors, but a dark room with one skylight is lit only in its shaft. Without this the whole room glowed with
    // the sky's light for having fog in it.
    float skyOpen = 0.0;
    for (int probe = 0; probe < 3; ++probe) {
        vec3 probePos = origin + rayDir * (min(endDistance, volumetricMediumMaxDistance()) * (float(probe) + 0.5) / 3.0);
        skyOpen += volumetricSkyVisibility(probePos, insideBoat);
    }
    skyAmbient *= skyOpen / 3.0;
    vec3 airAmbient = skyAmbient * airSigmaT * 0.35;

    // One cloud shadow for the whole ray, taken halfway along the march.
    vec3 midPoint = origin + rayDir * (marchEnd * 0.5) + vec3(worldUBO.cameraPos.xyz);
    float cloudVisibility =
        volumetricCloudLightVisibility(midPoint, lightDir, max(VPT_VOLUMETRIC_CLOUD_LIGHT_STEPS, 1), 0.175);

    // A short stretch of air (a wall a few blocks away) needs far fewer steps than the full distance; the count
    // follows the square root of the length, as the steps themselves are packed near the camera.
    int maxSteps = max(VPT_VOLUMETRIC_LIGHT_MAX_STEPS, 1);
    int stepCount =
        clamp(int(ceil(float(maxSteps) * sqrt(marchEnd / volumetricMediumMaxDistance()))), min(4, maxSteps), maxSteps);
    // In full rain the sun and moon are gone, and a shadow ray per step would only find that out.
    bool directLight = skyUBO.rainGradient < 0.98;
    uint seed = xxhash32(uvec3(uint(gl_LaunchIDEXT.x), uint(gl_LaunchIDEXT.y), worldUBO.seed ^ 0xBB67AE85u));
    float jitter = rand(seed);
    vec3 litSum = vec3(0.0);
    float litWeight = 0.0;
    float marchT = 0.0;
    for (int i = 0; i < stepCount; ++i) {
        float nextT = marchEnd * volumetricStepFraction(float(i + 1), float(stepCount));
        float stepLength = nextT - marchT;
        if (stepLength <= 1e-4) { continue; }
        float t = marchT + stepLength * jitter;
        vec3 position = origin + rayDir * t;
        vec3 lightRadiance = directLight ?
                                 sampleVolumetricDirectionalLight(position, lightDir, insideBoat) * cloudVisibility :
                                 vec3(0.0);

        float localBiome =
            t < biomeReach ? biomeDensity * biomeFogHeightProfile(position.y + float(worldUBO.cameraPos.y)) : 0.0;
        vec3 localBiomeT = biomeChroma * localBiome;
        vec3 localSigmaT = airSigmaT + localBiomeT;
        vec3 segmentTransmittance = exp(-localSigmaT * stepLength);
        vec3 segment = segmentIntegral(localSigmaT, segmentTransmittance, stepLength);
        // Light shafts: the air near the camera scatters the sun it is lit by, so a beam through a skylight or a
        // gap in the leaves shows as it does in Bedrock RTX, however clear the biome's fog. Only the sun's light (a
        // dark room stays dark around the beam) and only within LIGHT_SHAFT_REACH, so the distance does not haze.
        float shaftDensity = max(VPT_LIGHT_SHAFTS, 0.0) * (1.0 - smoothstep(LIGHT_SHAFT_REACH * 0.6, LIGHT_SHAFT_REACH, t));
        vec3 inScatter = lightRadiance * (airSigmaS + vec3(shaftDensity * shaftPhase)) + airAmbient;
        inScatter += (lightRadiance * biomePhase * biomeSunScatter + skyAmbient) * biomeAlbedo * localBiomeT;
        radiance += transmittance * inScatter * segment;
        transmittance *= segmentTransmittance;
        marchT = nextT;
        // The light the far medium is lit by: the average the march saw, weighted to its far half so a shadow
        // right in front of the camera does not darken the whole distance.
        float weight = float(i + 1);
        litSum += lightRadiance * weight;
        litWeight += weight;

        if (max(transmittance.r, max(transmittance.g, transmittance.b)) < 1e-3) { break; }
    }
    vec3 farLight = litWeight > 0.0 ? litSum / litWeight : vec3(0.0);

    // Past the march: the air to the hit (not to the sky, whose own atmosphere already carries it), and the biome fog
    // out to its reach, lit by that average light.
    if (max(transmittance.r, max(transmittance.g, transmittance.b)) >= 1e-3) {
        float airTail = max(endDistance - marchEnd, 0.0);
        float biomeTail = max(biomeReach - marchT, 0.0);
        float tailMidY = origin.y + rayDir.y * (marchT + biomeTail * 0.5) + float(worldUBO.cameraPos.y);
        vec3 biomeTailT = biomeChroma * biomeDensity * biomeFogHeightProfile(tailMidY);
        vec3 tailOpticalDepth = airSigmaT * airTail + biomeTailT * biomeTail;
        vec3 tailTransmittance = exp(-tailOpticalDepth);
        vec3 tailScatterDepth = (farLight * airSigmaS + airAmbient) * airTail +
                                (farLight * biomePhase * biomeSunScatter + skyAmbient) * biomeAlbedo * biomeTailT * biomeTail;
        vec3 tailFraction = vec3(tailOpticalDepth.r > 1e-6 ? (1.0 - tailTransmittance.r) / tailOpticalDepth.r : 1.0,
                                 tailOpticalDepth.g > 1e-6 ? (1.0 - tailTransmittance.g) / tailOpticalDepth.g : 1.0,
                                 tailOpticalDepth.b > 1e-6 ? (1.0 - tailTransmittance.b) / tailOpticalDepth.b : 1.0);
        radiance += transmittance * tailScatterDepth * tailFraction;
        transmittance *= tailTransmittance;
    }

    radiance = softClampVolumetricLight(radiance);
    transmittance = clamp(transmittance, vec3(0.0), vec3(1.0));
}

#endif // VPT_PT_VOLUMETRIC_GLSL
