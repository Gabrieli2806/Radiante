#ifndef VPT_FOG_MARCH_GLSL
#define VPT_FOG_MARCH_GLSL

// Each pixel's march through the volumetric fog's world grids (common/froxel_fog.glsl), run by froxel/march.comp
// after the world pass has relit its share of the cells (pt/pt_froxel.glsl). It used to run inside the world pass's
// ray generation shader: there it held registers the whole path tracer then lacked, and cost one and a half to two
// milliseconds whatever the number of steps; on its own it costs a fraction of that.
//
// Needs worldUBO, skyUBO, skyFull, finiteFloat / finiteVec3, common/celestial.glsl, hooks/fog.glsl,
// common/fog_medium.glsl and common/froxel_fog.glsl.

#include "common/biome_fog_profile.glsl"

layout(set = 5, binding = 42) uniform sampler3D fogSunNearTexture;
layout(set = 5, binding = 43) uniform sampler3D fogBlockNearTexture;
layout(set = 5, binding = 47) uniform sampler3D fogSunFarTexture;

// What the world pass keeps of a pixel for its march: the distance marched (0: no fog), whether it ends in the sky,
// how far the biome's fog reaches along it, the tint of glass it looked through, and whether the pixel's fog is
// composed (not with a transparent surface's split). As bits, so nothing is rounded or flushed on the way.
uvec4 fogMarchRecordOf(float distance, bool hitSky, float biomeReach, vec3 glassTint, bool transparentSplit) {
    return uvec4(floatBitsToUint(max(distance, 1e-3)), floatBitsToUint(biomeReach),
                 packUnorm4x8(vec4(clamp(glassTint, vec3(0.0), vec3(1.0)), 0.0)),
                 (hitSky ? 1u : 0u) | (transparentSplit ? 2u : 0u));
}

// How much of the block lights the air scatters: a soft glow, not a lit wall of haze across a street of signs.
const float FROXEL_BLOCK_LIGHT_GLOW = 0.12;

// Past the fog's range, over how many blocks a shadow at its far edge fades out of the air behind it.
const float FROXEL_TAIL_SHADOW_REACH = 24.0;

// Steps each pixel takes through the grids, packed near the camera (fewer over a short stretch, one and a half a
// block). The grids are smooth between cells, and the steps' start moves every frame, so the upscaler's history
// fills in between them.
const int FOG_VOLUME_STEPS = 16;
// The march stops once this little of the light behind still shows through.
const float FOG_VOLUME_MIN_TRANSMITTANCE = 0.02;
// Over how many fine cells (two a block) the fine grid hands over to the coarse one at its edge.
const float FOG_VOLUME_HANDOVER = 4.0;

// The medium a pixel's ray crosses, as factors of what reaches each point: the light it scatters towards the eye per
// block is sun * (sunAir + sunShaft * shaftFade + (sunFog + sunFog2 * h) * h) + sky * (skyAir + skyFog * h)
// + block * (blockAir + blockShaft * shaftFade + blockFog * h), with h the biome fog's height profile and shaftFade
// the light shafts' fade with distance.
struct FogMedium {
    vec3 sunAir;
    float sunShaft;
    vec3 sunFog;
    vec3 sunFog2;
    vec3 skyAir;
    vec3 skyFog;
    vec3 blockAir;
    float blockShaft;
    vec3 blockFog;
};

// Whether the pack's (biome's) fog is in the air at all; without it Bedrock's fog is only the light shafts.
bool fogVolumeHasBiomeFog() {
    return biomeFogDensity() * max(skyUBO.biomeFog.r, max(skyUBO.biomeFog.g, skyUBO.biomeFog.b)) > 0.0;
}

// Whether there is anything in the air to light: Bedrock's fog without the pack's fog and without light shafts (set
// to none, or gone in full rain, which hides the sun) has none, and then neither the cells nor the pixels do any
// work. In rain every pixel used to march an empty volume.
bool fogVolumeHasMedium() {
#if VPT_BEDROCK_FOG != 0
    float shafts = max(VPT_LIGHT_SHAFTS, 0.0) * max(skyUBO.fogControls.z, 0.0);
    return (shafts > 0.0 && skyUBO.rainGradient < 0.98) || fogVolumeHasBiomeFog();
#else
    return true;
#endif
}

// How far out the air has anything to show: with Bedrock's fog and no biome fog, only the light shafts, which fade
// out by LIGHT_SHAFT_REACH.
float fogVolumeReach() {
#if VPT_BEDROCK_FOG != 0
    if (!fogVolumeHasBiomeFog()) { return min(froxelRange(), LIGHT_SHAFT_REACH); }
#endif
    return froxelRange();
}

FogMedium fogMedium(vec3 rayDir) {
    FogMedium medium;
    vec3 lightDir = celestialLightDirection();
    float cosTheta = clamp(dot(rayDir, lightDir), -1.0, 1.0);
    float shaftPhase = mix(INV_4_PI, min(henyeyGreenstein(cosTheta, 0.5), 2.0), 0.5);
#if VPT_BEDROCK_FOG != 0
    // Bedrock RTX's volumetric fog (observed structure): the pack's air medium, lit by the sun with
    // Henyey-Greenstein scattering normalised by 1/pi, and by the sky; the sunlit part scales with the square of the
    // height profile, the sky part with it once. The anisotropy g is a run-time value in Bedrock (UNKNOWN;
    // VPT_BEDROCK_FOG_G). Light shafts as Radiante's own air has them: the air near the camera scatters the sunlight
    // it is lit by, sun only, so a dark room stays dark around the beam; Light Shafts and Volumetric Fog Strength set
    // how much. (The pack's fog alone is zero without Biome Ambiance, and a few thousandths a block with it.)
    vec3 sigmaS = biomeFogChroma() * biomeFogDensity() * clamp(skyUBO.biomeFog.rgb, vec3(0.0), vec3(1.0));
    float g = clamp(VPT_BEDROCK_FOG_G, -0.95, 0.95);
    float phase = (1.0 - g * g) / pow(abs(1.0 + g * g - 2.0 * g * cosTheta), 1.5) / PI;
    medium.sunAir = vec3(0.0);
    medium.sunShaft = max(VPT_LIGHT_SHAFTS, 0.0) * max(skyUBO.fogControls.z, 0.0) * shaftPhase;
    // In full rain the sun is gone (sampleVolumetricDirectionalLight).
    if (skyUBO.rainGradient >= 0.98) { medium.sunShaft = 0.0; }
    medium.sunFog = vec3(0.0);
    medium.sunFog2 = sigmaS * phase;
    medium.skyAir = vec3(0.0);
    medium.skyFog = sigmaS * biomeFogAmbient(rayDir) / PI;
    medium.blockAir = vec3(0.0);
    medium.blockShaft = 0.0;
    medium.blockFog = vec3(0.0);
#else
    // Radiante's own air (the per-pixel march's medium, integrateVolumetricFog): the air, its light shafts and the
    // biome's fog. Block lights scatter evenly in every direction, by the shafts' density near the camera, the
    // biome's fog and the air.
    vec3 sigmaS;
    vec3 sigmaT;
    volumetricAirMedium(cosTheta, sigmaS, sigmaT);
    float airDensity = volumetricAirDensity();
    float fogStrength = max(skyUBO.fogControls.z, 0.0);
    vec3 airSigmaS = sigmaS * airDensity * max(VPT_VOLUMETRIC_LIGHT_SCATTERING_STRENGTH, 0.0) * fogStrength;
    vec3 airSigmaT = sigmaT * airDensity * 0.45 * fogStrength;
    vec3 biome = biomeFogChroma() * biomeFogDensity() * clamp(skyUBO.biomeFog.rgb, vec3(0.0), vec3(1.5));
    float biomePhase = mix(INV_4_PI, min(henyeyGreenstein(cosTheta, VPT_BIOME_FOG_FORWARD), 4.0), 0.6);
    vec3 ambient = biomeFogAmbient(rayDir);
    float glow = INV_4_PI * FROXEL_BLOCK_LIGHT_GLOW;
    medium.sunAir = airSigmaS;
    medium.sunShaft = max(VPT_LIGHT_SHAFTS, 0.0) * shaftPhase;
    medium.sunFog = biome * biomePhase * max(VPT_BIOME_FOG_SCATTERING, 0.0);
    medium.sunFog2 = vec3(0.0);
    medium.skyAir = ambient * airSigmaT * 0.35;
    medium.skyFog = ambient * biome;
    medium.blockAir = airSigmaT * 0.35 * glow;
    medium.blockShaft = max(VPT_LIGHT_SHAFTS, 0.0) * glow;
    medium.blockFog = biome * glow;
#endif
    return medium;
}

vec3 fogMediumSource(FogMedium medium, vec3 position, float distance, vec4 sunSky, vec3 block) {
    float h = biomeFogHeightProfile(position.y + float(worldUBO.cameraPos.y));
    float shaftFade = 1.0 - smoothstep(LIGHT_SHAFT_REACH * 0.6, LIGHT_SHAFT_REACH, distance);
    return sunSky.rgb * (medium.sunAir + medium.sunShaft * shaftFade + (medium.sunFog + medium.sunFog2 * h) * h) +
           sunSky.a * (medium.skyAir + medium.skyFog * h) +
           block * (medium.blockAir + medium.blockShaft * shaftFade + medium.blockFog * h);
}

// Where the camera is in both grids, for reading them.
struct FogGrids {
    vec3 nearFraction;
    vec3 nearOrigin;
    vec3 farFraction;
    vec3 farOrigin;
};

FogGrids fogGrids() {
    FogGrids grids;
    ivec3 nearCell = fogVolumeCameraCell(worldUBO.cameraPos.xyz, 0, grids.nearFraction);
    ivec3 farCell = fogVolumeCameraCell(worldUBO.cameraPos.xyz, 1, grids.farFraction);
    // The camera's cell's texel (the sampler wraps round, as the grid does).
    grids.nearOrigin = vec3(fogVolumeTexel(nearCell));
    grids.farOrigin = vec3(fogVolumeTexel(farCell));
    return grids;
}

// What reaches a point (camera-relative) from the grids: the sun (rgb) and the sky (a), and the block lights. The fine
// grid where it reaches, handing over to the coarse one at its edge; past the coarse one, its edge.
void fogGridLight(FogGrids grids, vec3 position, out vec4 sunSky, out vec3 block) {
    const vec3 inner = vec3(FOG_VOLUME_EXTENT / 2) - 0.5;
    vec3 near = grids.nearFraction + position / FOG_VOLUME_NEAR_CELL;
    vec3 edge = inner - abs(near);
    float nearWeight = clamp(min(edge.x, min(edge.y, edge.z)) / FOG_VOLUME_HANDOVER, 0.0, 1.0);
    sunSky = vec4(0.0);
    block = vec3(0.0);
    if (nearWeight < 1.0) {
        vec3 far = clamp(grids.farFraction + position / FOG_VOLUME_FAR_CELL, -inner, inner);
        sunSky = textureLod(fogSunFarTexture, (grids.farOrigin + far) / vec3(FOG_VOLUME_EXTENT), 0.0);
    }
    if (nearWeight > 0.0) {
        vec3 uvw = (grids.nearOrigin + near) / vec3(FOG_VOLUME_EXTENT);
        sunSky = mix(sunSky, textureLod(fogSunNearTexture, uvw, 0.0), nearWeight);
#if VPT_BEDROCK_FOG == 0
        block = textureLod(fogBlockNearTexture, uvw, 0.0).rgb * nearWeight;
#endif
    }
    if (!finiteVec3(sunSky.rgb) || !finiteFloat(sunSky.a)) { sunSky = vec4(0.0); }
    if (!finiteVec3(block)) { block = vec3(0.0); }
    sunSky = max(sunSky, vec4(0.0));
    block = max(block, vec3(0.0));
}

// A step's distance, as a share w of the march: packed near the camera, as Bedrock's slices are over its 50 blocks
// (5 (11^w - 1)), and about even over a short stretch.
float fogMarchDistance(float w, float marchEnd) {
    float curve = max(marchEnd / 5.0, 1e-3);
    return marchEnd * (pow(curve + 1.0, w) - 1.0) / curve;
}

// The fog between the camera and endDistance along this pixel's ray, marched through the grids; past the fog's range
// the medium carries on lit as its far edge. Every step reads the light where it lies in the world, so turning the
// camera only changes which rays are marched, not the light they cross. The steps start at an offset that changes
// from pixel to pixel and frame to frame, which the upscaler's history averages out.
void froxelFog(vec3 origin, vec3 rayDir, ivec2 pixel, float endDistance, float biomeReach, bool hitSky,
               out vec3 transmittance, out vec3 radiance) {
    transmittance = vec3(1.0);
    radiance = vec3(0.0);
    if (!finiteFloat(endDistance) || endDistance <= 0.05 || !fogVolumeHasMedium()) { return; }
    float range = froxelRange();
    float marchEnd = min(endDistance, fogVolumeReach());
    FogMedium medium = fogMedium(rayDir);
    FogGrids grids = fogGrids();
    int steps = clamp(int(ceil(marchEnd * 1.5)), 4, FOG_VOLUME_STEPS);
    float offset = fract(52.9829189 * fract(dot(vec2(pixel), vec2(0.06711056, 0.00583715))) +
                         float(worldUBO.frameCounter & 1023u) * 0.6180339887);

    vec3 inscatter = vec3(0.0);
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0 && VPT_BEDROCK_FOG != 0
    vec3 rgbTransmittance = vec3(1.0);
#else
    float t = 1.0;
#endif
    float previous = 0.0;
    for (int i = 0; i < steps; i++) {
        float next = fogMarchDistance(float(i + 1) / float(steps), marchEnd);
        float distance = fogMarchDistance((float(i) + offset) / float(steps), marchEnd);
        float stepLength = max(next - previous, 0.0);
        previous = next;
        vec3 position = origin + rayDir * distance;
        vec4 sunSky;
        vec3 block;
        fogGridLight(grids, position, sunSky, block);
        vec3 source = fogMediumSource(medium, position, distance, sunSky, block);
        float worldY = position.y + float(worldUBO.cameraPos.y);
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0 && VPT_BEDROCK_FOG != 0
        inscatter += rgbTransmittance * source * stepLength;
        rgbTransmittance *= exp(-froxelBedrockExtinctionRGB(worldY) * stepLength);
        if (max(rgbTransmittance.r, max(rgbTransmittance.g, rgbTransmittance.b)) < FOG_VOLUME_MIN_TRANSMITTANCE) {
            break;
        }
#else
        float extinction = froxelExtinction(worldY);
        float stepTransmittance = exp(-extinction * stepLength);
        inscatter += t * source * (1.0 - stepTransmittance) / extinction;
        t *= stepTransmittance;
        if (t < FOG_VOLUME_MIN_TRANSMITTANCE) { break; }
#endif
    }
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0 && VPT_BEDROCK_FOG != 0
    if (!finiteVec3(inscatter) || !finiteVec3(rgbTransmittance)) { return; }
    radiance = max(inscatter, vec3(0.0));
    transmittance = clamp(rgbTransmittance, vec3(0.0), vec3(1.0));
#else
    if (!finiteVec3(inscatter) || !finiteFloat(t)) { return; }
    radiance = max(inscatter, vec3(0.0));

    // Past the range the medium carries on, lit as the grids have it at the range's edge. (Bedrock's own fog was
    // taken to stop at its volume's far edge, 50 blocks out: every hill beyond stood equally clear behind a fogged
    // foreground, and with the haze as faint as it now is, 50 blocks of it showed nothing of a biome's air at all.)
    // (Without biome fog Bedrock's air ends with its shafts: there is no tail to add.)
    float tail = fogVolumeReach() < range ? 0.0 : max(max(endDistance, biomeReach) - range, 0.0);
    if (tail > 0.0 && t > 1e-3) {
        vec3 edgePosition = origin + rayDir * range;
        vec4 sunSky;
        vec3 block;
        fogGridLight(grids, edgePosition, sunSky, block);
        vec3 far = max(fogMediumSource(medium, edgePosition, range, sunSky, block), vec3(0.0));
        float extinction = froxelExtinction(edgePosition.y + float(worldUBO.cameraPos.y));
        float tailTransmittance = exp(-extinction * tail);
        float tailDepth = (1.0 - tailTransmittance) / extinction;
        vec3 tailLight = far * tailDepth;
        if (hitSky) {
            // Towards the sky the air behind the range is open: a shadow that falls on its far edge does not run on
            // through all of it. Lit whole as that edge, a tower between the camera and the sun stood a second time
            // in the sky, a dark, stepped copy of itself cut out of the haze around the sun. The edge's own light
            // fades into the open air's over FROXEL_TAIL_SHADOW_REACH.
            float edgeRate = extinction + 1.0 / FROXEL_TAIL_SHADOW_REACH;
            float edgeDepth = (1.0 - exp(-edgeRate * tail)) / edgeRate;
            // The open air's light is the coarse grid's top layer above the edge, 64 blocks over the camera: over
            // the buildings and trees around it, with the sun's colour and the clouds' shadow as the cells have them.
            vec4 openSunSky;
            vec3 openBlock;
            fogGridLight(grids, vec3(edgePosition.x, 1e4, edgePosition.z), openSunSky, openBlock);
            vec3 openSun = openSunSky.rgb;
            vec3 open = fogMediumSource(medium, edgePosition, range, vec4(openSun, 1.0), vec3(0.0));
            if (finiteVec3(open)) { tailLight = far * edgeDepth + max(open, vec3(0.0)) * (tailDepth - edgeDepth); }
        }
        if (finiteVec3(tailLight)) {
            radiance += t * tailLight;
            t *= tailTransmittance;
        }
    }
    radiance = softClampVolumetricLight(radiance);
    transmittance = vec3(t);
#endif
}
#endif
