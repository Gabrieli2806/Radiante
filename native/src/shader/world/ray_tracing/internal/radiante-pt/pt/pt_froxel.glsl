#ifndef VPT_PT_FROXEL_GLSL
#define VPT_PT_FROXEL_GLSL

// Volumetric fog in the world grids (common/froxel_fog.glsl): this thread's share of the cells relit this frame.
// Each pixel's march through the grids is froxel/march.comp's (common/fog_march.glsl), after this pass. Only
// compiled with the fog's grid on. The grids are declared here rather than in pt_bindings.glsl (see the note there).

#if VPT_FROXEL_ACTIVE
#    include "common/froxel_fog.glsl"
#    include "common/block_light_data.glsl"
#    include "common/fog_march.glsl"

// Written where this pass relights a cell; froxel/march.comp reads them through samplers after the pass.
layout(set = 5, binding = 40, rgba16f) uniform image3D fogSunNearImage;
layout(set = 5, binding = 41, rgba16f) uniform image3D fogBlockNearImage;
layout(set = 5, binding = 46, rgba16f) uniform image3D fogSunFarImage;
layout(set = 5, binding = 49, rgba16f) uniform image3D fogBlockFarImage;
// Per pixel, what froxel/march.comp needs to march its fog (common/fog_march.glsl, fogMarchRecord*).
layout(set = 5, binding = 50, rgba32ui) uniform writeonly uimage2D fogMarchRecordImage;

void clearFogMarchRecord(ivec2 pixel) {
    imageStore(fogMarchRecordImage, pixel, uvec4(0u));
}

void writeFogMarchRecord(ivec2 pixel, float distance, bool hitSky, float biomeReach, vec3 glassTint,
                         bool transparentSplit) {
    imageStore(fogMarchRecordImage, pixel, fogMarchRecordOf(distance, hitSky, biomeReach, glassTint, transparentSplit));
}


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

// How much of a cell's light each fresh look brings in once it has had a few (before that, an even average of its
// looks). A fine cell in view within 16 blocks is looked at every fourth frame, one further out every eighth, and one
// out of view or in the coarse grid every sixteenth, at another point in it each time: at 0.1 its light averages
// about ten points, half a second at 60 fps near the camera.
const float FOG_VOLUME_BLEND = 0.1;
const float FOG_VOLUME_MAX_LOOKS = 64.0;
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
    // Further out, where a cell is a smaller part of the view and the light changes no faster: in view beyond 16
    // blocks every eighth frame, and the coarse grid every sixteenth.
    uint visitCount = worldUBO.frameCounter >> 2u;
    if (looks > 0.0 && cascade == 1 && (visitCount & 3u) != 0u) { return; }
    if (looks > 0.0 && cascade == 0 && dot(centre, centre) > 256.0 && (visitCount & 1u) != 0u) { return; }
    // Nothing past the air's reach is ever read (fogVolumeReach); a cell there keeps what it had.
    if (looks > 0.0 && length(centre) > fogVolumeReach() + 2.0 * cellSize) { return; }

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

#endif

#endif // VPT_PT_FROXEL_GLSL
