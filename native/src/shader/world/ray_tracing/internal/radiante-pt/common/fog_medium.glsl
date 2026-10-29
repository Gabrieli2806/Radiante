#ifndef VPT_FOG_MEDIUM_GLSL
#define VPT_FOG_MEDIUM_GLSL

// The air's medium as the volumetric fog sees it, without any ray tracing: the per-pixel march
// (pt/pt_volumetric.glsl) and the world grids' march (froxel/march.comp) both read it. Moved out of
// pt_volumetric.glsl as it was.
//
// Needs worldUBO, skyUBO, skyFull, common/celestial.glsl and hooks/fog.glsl.

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
    hookFog(cosTheta, sigmaS, sigmaT); // a pack's fog (hooks/fog.glsl); nothing in radiante-pt
}

#ifndef VPT_LIGHT_SHAFTS
#    define VPT_LIGHT_SHAFTS 0.015
#endif
const float LIGHT_SHAFT_REACH = 48.0;

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

// Per-biome fog as a participating medium, the way Bedrock RTX does it: the biome's density both absorbs and
// scatters, lit by the sun or moon through shadow rays (so trees and terrain cast light shafts into it) and by the sky
// all around. Only in the overworld; skyUBO.biomeFog.a is already zero underground and when the option is off.
float biomeFogDensity() {
    return worldUBO.skyType == 1 ? max(skyUBO.biomeFog.a, 0.0) : 0.0;
}

vec3 biomeFogAmbient(vec3 rayDir) {
    vec3 horizon = texture(skyFull, normalize(vec3(rayDir.x, 0.12, rayDir.z))).rgb;
    vec3 zenith = texture(skyFull, vec3(0.0, 1.0, 0.0)).rgb;
    return max((horizon + zenith) * 0.5, vec3(0.0));
}

#endif
