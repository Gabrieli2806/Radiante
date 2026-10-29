#ifndef VPT_PT_FROXEL_GLSL
#define VPT_PT_FROXEL_GLSL

// Volumetric fog in the world grids (common/froxel_fog.glsl): this thread's share of the cells relit this frame, and
// each pixel's march through the grids. Only compiled with the fog's grid on. The grids are declared here rather
// than in pt_bindings.glsl (see the note there).

#if VPT_FROXEL_ACTIVE
#    include "common/froxel_fog.glsl"
#    include "common/block_light_data.glsl"

// Written where this pass relights a cell, and read through the samplers by every pixel's march in the same pass: a
// pixel may read a cell another thread is rewriting, and either value is that cell's light, a frame apart.
layout(set = 5, binding = 40, rgba16f) uniform image3D fogSunNearImage;
layout(set = 5, binding = 41, rgba16f) uniform image3D fogBlockNearImage;
layout(set = 5, binding = 46, rgba16f) uniform image3D fogSunFarImage;
layout(set = 5, binding = 49, rgba16f) uniform image3D fogBlockFarImage;
layout(set = 5, binding = 42) uniform sampler3D fogSunNearTexture;
layout(set = 5, binding = 43) uniform sampler3D fogBlockNearTexture;
layout(set = 5, binding = 47) uniform sampler3D fogSunFarTexture;

// How much of the block lights the air scatters: a soft glow, not a lit wall of haze across a street of signs.
const float FROXEL_BLOCK_LIGHT_GLOW = 0.12;

// Light reaching a point in the air from the block lights around it (torches, lanterns, glowstone...), picked as the
// surfaces pick them (block_light.glsl) but without a surface: the fog glows around each light.
vec3 froxelBlockLight(vec3 position, inout uint seed) {
    if (worldUBO.blockLightSampling == 0u) { return vec3(0.0); }
    // The eight sections nearest the point (the 2x2x2 block around its closest section corner), not all 27 around
    // it: lights within about 8 to 24 blocks, which is as far as a glow in the air carries.
    int slots[9];
    int slotCount = 0;
    ivec3 base = ivec3(floor((position + vec3(worldUBO.cameraPos.xyz) - 8.0) / 16.0));
    ivec3 center = vptSectionOf(position);
    uint neighbours;
    if (vptLightNeighbours(center, neighbours)) {
        VPT_APPEND_NEIGHBOUR_SLOTS(center, neighbours & vptNearNeighbourBits(base, center), slots, slotCount)
    } else {
        for (int corner = 0; corner < 8; corner++) {
            int slot = vptSectionLightSlot(base + ivec3(corner & 1, (corner >> 1) & 1, corner >> 2));
            if (slot >= 0) { slots[slotCount++] = slot; }
        }
    }
    if (vptHasEntityLights()) { slots[slotCount++] = VPT_ENTITY_LIGHT_SLOT; }
    if (slotCount == 0) { return vec3(0.0); }

    // Four lights looked at for the one traced: the lights cost only a read each, and with two the glow around a
    // street of lanterns sparkled from one look to the next.
    const int CANDIDATES = 4;
    vec3 cameraPos = vec3(worldUBO.cameraPos.xyz);
    float weightSum = 0.0;
    float chosenTarget = 0.0;
    vec3 chosenContribution = vec3(0.0);
    vec3 chosenDir = vec3(0.0);
    float chosenDistance = 0.0;
    for (int i = 0; i < CANDIDATES; i++) {
        int slot = slots[min(int(rand(seed) * float(slotCount)), slotCount - 1)];
        uint slotLightCount = vptSlotLightCount(slot);
        uint lightIndex = min(uint(rand(seed) * float(slotLightCount)), slotLightCount - 1u);
        VptPackedLight light = VptPackedLightBuffer(vptSlotLightAddress(slot)).lights[lightIndex];
        float area = light.p0Area.w;
        if (area <= 1e-8) { continue; }
        bool pointLight = vptIsPointLight(light);
        vec3 point = pointLight ? light.p0Area.xyz - cameraPos :
                                  vptSampleTrianglePoint(light.p0Area.xyz, light.p1.xyz, light.p2.xyz,
                                                         vec2(rand(seed), rand(seed))) - cameraPos;
        vec3 toLight = point - position;
        float distance2 = dot(toLight, toLight);
        if (distance2 <= 1e-6) { continue; }
        float lightDistance = sqrt(distance2);
        vec3 dir = toLight / lightDistance;
        float cosLight = pointLight ? 1.0 : dot(light.normal.xyz, -dir);
        if (cosLight <= 0.0) { continue; }
        // Kept from growing without bound right next to a light, where one cell would flare.
        vec3 radiance = pointLight ? light.radiance.rgb * vptPointLightScale() :
                        (light.radiance.w > 0.5 ? light.radiance.rgb * vptExactLightScale() : light.radiance.rgb);
#ifndef RADIANTE_HOOK_BLOCK_LIGHT_DEFAULT
        hookBlockLight(radiance, uint(clamp(light.radiance.w + 0.5, 0.0, 2.0))); // hooks/block_light.glsl
#endif
        vec3 contribution = radiance * (cosLight / max(distance2, 0.5));
        float target = dot(contribution, vec3(0.2126, 0.7152, 0.0722));
        if (target <= 1e-10) { continue; }
        float sourcePdf = 1.0 / (float(slotCount) * float(slotLightCount) * area);
        float weight = target / sourcePdf;
        weightSum += weight;
        if (rand(seed) * weightSum < weight) {
            chosenTarget = target;
            chosenContribution = contribution;
            chosenDir = dir;
            chosenDistance = lightDistance;
        }
    }
    if (chosenTarget <= 0.0) { return vec3(0.0); }

    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = 0u;
    shadowRay.pad0 = BLOCK_LIGHT_QUERY;
    traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS, WORLD_MASK | PLAYER_MASK, 0, 0, 0, position, 0.001, chosenDir,
                max(chosenDistance - 0.02, 0.002), 1);
    shadowRay.pad0 = 0u;
    vec3 visibility = shadowRay.radiance * shadowRay.throughput;
    vec3 estimate = chosenContribution / chosenTarget * (weightSum / float(CANDIDATES));
    return VPT_INDIRECT_LIGHT_STRENGTH * worldUBO.emissionBrightness * estimate * visibility;
}

// How far above a point its light is taken from when the point stands for open air (see froxelFog's tail): over
// every building and hill.
const float FROXEL_OPEN_AIR_LIFT = 500.0;
// Past the fog's range, over how many blocks a shadow at its far edge fades out of the air behind it.
const float FROXEL_TAIL_SHADOW_REACH = 24.0;

// How much of a cell's light each fresh look brings in once it has had a few (before that, an even average of its
// looks). A cell in view is looked at every fourth frame and one out of view every sixteenth, at another point in it
// each time: at 0.1 its light averages about ten points, half a second at 60 fps.
const float FOG_VOLUME_BLEND = 0.1;
const float FOG_VOLUME_MAX_LOOKS = 64.0;
// Steps each pixel takes through the grids, packed near the camera (fewer over a short stretch, two a block). The
// grids are smooth between cells, so the steps only need to be about a cell apart where the cells are.
const int FOG_VOLUME_STEPS = 24;
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

// Whether there is anything in the air to light: Bedrock's fog without light shafts and without the pack's fog has
// none, and its cells are then left alone.
bool fogVolumeHasMedium() {
#if VPT_BEDROCK_FOG != 0
    float shafts = max(VPT_LIGHT_SHAFTS, 0.0) * max(skyUBO.fogControls.z, 0.0);
    float fog = biomeFogDensity() * max(skyUBO.biomeFog.r, max(skyUBO.biomeFog.g, skyUBO.biomeFog.b));
    return shafts > 0.0 || fog > 0.0;
#else
    return true;
#endif
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

// Relights one cell: the sun through a shadow ray, the sky straight up and (fine grid, Radiante's air) one block
// light, each from a point that steps through the cell from one look to the next, blended into what the cell held.
void relightFogCell(int cascade, ivec3 relative, ivec3 cameraCell, vec3 cameraFraction, bool needSky) {
    float cellSize = fogVolumeCellSize(cascade);
    ivec3 cell = cameraCell + relative;
    ivec3 texel = fogVolumeTexel(cell);
    vec4 sunHistory = cascade == 0 ? imageLoad(fogSunNearImage, texel) : imageLoad(fogSunFarImage, texel);
    vec4 blockHistory = cascade == 0 ? imageLoad(fogBlockNearImage, texel) : imageLoad(fogBlockFarImage, texel);
    if (!finiteVec3(sunHistory.rgb) || !finiteFloat(sunHistory.a) || !finiteVec3(blockHistory.rgb) ||
        !finiteFloat(blockHistory.a)) {
        sunHistory = vec4(0.0);
        blockHistory = vec4(0.0);
    }
    float looks = max(blockHistory.a, 0.0);

    // Cells out of view are looked at a quarter as often (a turn of the camera shows them a little older, never
    // anywhere else), unless they have never been looked at.
    vec3 centre = (vec3(relative) + 0.5 - cameraFraction) * cellSize;
    vec4 clip = worldUBO.cameraProjMat * (worldUBO.cameraEffectedViewMat * vec4(centre, 1.0));
    bool inView = all(lessThan(abs(clip.xy), vec2(max(clip.w, 0.0) * 1.2 + 2.0 * cellSize)));
    if (!inView && looks > 0.0 && ((worldUBO.frameCounter >> 2u) & 3u) != 0u) { return; }

    // Where in the cell it is lit this time: a fixed offset per world cell stepped along the R3 sequence each time
    // the cell comes round, so the average is over points spread evenly through the cell, not white noise. A
    // shadow's edge across a cell (a window frame, a branch) is in or out at each point, and its share of the cell
    // comes out as the cell's light.
    uint cellHash = xxhash32(uvec3(uint(cell.x), uint(cell.y) ^ (uint(cascade) << 30u), uint(cell.z)));
    vec3 cellOffset = vec3(uvec3(cellHash, cellHash >> 11u, cellHash >> 22u) & uvec3(2047u, 2047u, 1023u)) /
                      vec3(2048.0, 2048.0, 1024.0);
    float visit = float((worldUBO.frameCounter >> 2u) & 4095u);
    vec3 jitter = fract(cellOffset + visit * vec3(0.8191725134, 0.6710436067, 0.5497004779));
    vec3 position = (vec3(relative) + jitter - cameraFraction) * cellSize;

    vec3 lightDir = celestialLightDirection();
    vec3 sun = vec3(0.0);
    if (skyUBO.rainGradient < 0.98) {
        sun = sampleVolumetricDirectionalLight(position, lightDir, false);
#if VPT_BEDROCK_FOG == 0
        sun *= volumetricCloudLightVisibility(position + vec3(worldUBO.cameraPos.xyz), lightDir, 2, 0.175);
#endif
    }
    float sky = needSky ? volumetricSkyVisibility(position, false) : (looks > 0.0 ? sunHistory.a : 1.0);
    vec3 block = vec3(0.0);
#if VPT_BEDROCK_FOG == 0
    if (cascade == 0) {
        uint seed = xxhash32(uvec3(uint(texel.x) | (uint(texel.y) << 16u), uint(texel.z),
                                   worldUBO.seed ^ 0x6A09E667u));
        block = froxelBlockLight(position, seed);
        // A rare bright pick (a light at a hand's length) is let in at a quarter past four times what the cell held:
        // it still shows if it stays, but does not flash.
        float historyLuma = dot(blockHistory.rgb, vec3(0.2126, 0.7152, 0.0722));
        float sampleLuma = dot(block, vec3(0.2126, 0.7152, 0.0722));
        float ceiling = historyLuma * 4.0 + 1e-4;
        if (looks > 0.0 && finiteFloat(sampleLuma) && sampleLuma > ceiling) {
            block *= (ceiling + (sampleLuma - ceiling) * 0.25) / sampleLuma;
        }
    }
#endif
    if (!finiteVec3(sun)) { sun = vec3(0.0); }
    if (!finiteVec3(block)) { block = vec3(0.0); }
    float blend = max(1.0 / (looks + 1.0), FOG_VOLUME_BLEND);
    vec4 sunSky = clamp(mix(sunHistory, vec4(sun, sky), blend), vec4(0.0), vec4(65504.0));
    vec4 blockLooks = clamp(vec4(mix(blockHistory.rgb, block, blend), min(looks + 1.0, FOG_VOLUME_MAX_LOOKS)),
                            vec4(0.0), vec4(65504.0));
    if (cascade == 0) {
        imageStore(fogSunNearImage, texel, sunSky);
        imageStore(fogBlockNearImage, texel, blockLooks);
    } else {
        imageStore(fogSunFarImage, texel, sunSky);
        imageStore(fogBlockFarImage, texel, blockLooks);
    }
}

// This thread's share of the cells relit this frame: in each grid a quarter of them, in a 2x2 pattern of world cells
// that shifts every frame and layer (froxel/shift.comp has already moved the grids with the camera).
void shadeFroxels() {
    if (VPT_VOLUMETRIC_LIGHT_MODE != VPT_VOLUMETRIC_LIGHT_MODE_VOLUMETRIC || worldUBO.skyType != 1) { return; }
    if (!fogVolumeHasMedium()) { return; }
#if VPT_BEDROCK_FOG != 0
    bool needSky = biomeFogDensity() > 0.0;
#else
    bool needSky = true;
#endif
    uint threadIndex = gl_LaunchIDEXT.y * gl_LaunchSizeEXT.x + gl_LaunchIDEXT.x;
    uint threadCount = gl_LaunchSizeEXT.x * gl_LaunchSizeEXT.y;
    const ivec3 half3 = FOG_VOLUME_EXTENT / 2;
    const uint perCascade = uint(half3.x * half3.y * FOG_VOLUME_EXTENT.z);
    vec3 nearFraction;
    vec3 farFraction;
    ivec3 nearCell = fogVolumeCameraCell(worldUBO.cameraPos.xyz, 0, nearFraction);
    ivec3 farCell = fogVolumeCameraCell(worldUBO.cameraPos.xyz, 1, farFraction);
    for (uint index = threadIndex; index < 2u * perCascade; index += threadCount) {
        int cascade = index >= perCascade ? 1 : 0;
        int local = int(index - uint(cascade) * perCascade);
        int layer = local / (half3.x * half3.y);
        int inLayer = local % (half3.x * half3.y);
        ivec3 cameraCell = cascade == 0 ? nearCell : farCell;
        int z = layer - half3.z;
        uint pattern = (worldUBO.frameCounter + uint(cameraCell.z + z)) & 3u;
        int x = (inLayer % half3.x) * 2 + ((int(pattern & 1u) - cameraCell.x) & 1) - half3.x;
        int y = (inLayer / half3.x) * 2 + ((int(pattern >> 1u) - cameraCell.y) & 1) - half3.y;
        relightFogCell(cascade, ivec3(x, y, z), cameraCell, cascade == 0 ? nearFraction : farFraction, needSky);
    }
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
void froxelFog(vec3 origin, vec3 rayDir, vec2 uv, float endDistance, float biomeReach, bool hitSky,
               out vec3 transmittance, out vec3 radiance) {
    transmittance = vec3(1.0);
    radiance = vec3(0.0);
    if (!finiteFloat(endDistance) || endDistance <= 0.05 || !fogVolumeHasMedium()) { return; }
    float range = froxelRange();
    float marchEnd = min(endDistance, range);
    FogMedium medium = fogMedium(rayDir);
    FogGrids grids = fogGrids();
    int steps = clamp(int(ceil(marchEnd * 2.0)), 4, FOG_VOLUME_STEPS);
    vec2 pixel = vec2(gl_LaunchIDEXT.xy);
    float offset = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))) +
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
#else
        float extinction = froxelExtinction(worldY);
        float stepTransmittance = exp(-extinction * stepLength);
        inscatter += t * source * (1.0 - stepTransmittance) / extinction;
        t *= stepTransmittance;
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
    float tail = max(max(endDistance, biomeReach) - range, 0.0);
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
            vec3 lightDir = celestialLightDirection();
            vec3 openSun = vec3(0.0);
            if (skyUBO.rainGradient < 0.98) {
                openSun = sampleVolumetricDirectionalLight(edgePosition + lightDir * FROXEL_OPEN_AIR_LIFT, lightDir,
                                                           false);
#    if VPT_BEDROCK_FOG == 0
                openSun *= volumetricCloudLightVisibility(edgePosition + vec3(worldUBO.cameraPos.xyz), lightDir, 2,
                                                          0.175);
#    endif
            }
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

#endif // VPT_PT_FROXEL_GLSL
