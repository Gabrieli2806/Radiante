#ifndef VPT_PT_SHARC_GLSL
#define VPT_PT_SHARC_GLSL

// SHaRC radiance cache, query side: the cache parameters and the rule for when a bounce may take the cached light
// instead of tracing on. Moved verbatim out of world.rgen.

#if USE_SHARC
SharcParameters makeSharcParameters() {
    HashMapData hashMapData;
    hashMapData.capacity = sharcConfig.capacity;
    hashMapData.hashEntriesBuffer = RWStructuredBuffer_uint64_t(sharcConfig.hashEntriesAddress);
#    if !HASH_GRID_ENABLE_64_BIT_ATOMICS
    hashMapData.lockBuffer = RWStructuredBuffer_uint(sharcConfig.lockAddress);
#    endif

    HashGridParameters gridParameters;
    gridParameters.cameraPosition = sharcConfig.cameraPosition.xyz;
    gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
    gridParameters.sceneScale = sharcConfig.sceneScale;
    gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;

    SharcParameters sharcParameters;
    sharcParameters.gridParameters = gridParameters;
    sharcParameters.hashMapData = hashMapData;
    sharcParameters.radianceScale = sharcConfig.radianceScale;
    sharcParameters.enableAntiFireflyFilter = sharcConfig.enableAntiFireflyFilter != 0;
    sharcParameters.accumulationBuffer = RWStructuredBuffer_SharcAccumulationData(sharcConfig.accumulationAddress);
    sharcParameters.resolvedBuffer = RWStructuredBuffer_SharcPackedData(sharcConfig.resolvedAddress);
    return sharcParameters;
}

bool sharcAllowQuery(bool isPrimaryCameraPath,
                     bool deepBounce,
                     float hitT,
                     float roughness,
                     uint lobeType,
                     SharcParameters sharcParameters,
                     vec3 positionWorld) {
    if (isPrimaryCameraPath) return false;
    // Cache for deep bounces: past the first bounce a diffuse hit takes the cached light however close it is, as
    // Bedrock RTX's irradiance cache does, instead of tracing on. Cheaper on every bounce, and the cost of a frame stops
    // growing with how much light bounces around.
    if (deepBounce && lobeType == 0u) return true;
    uint level = HashGridGetLevel(positionWorld, sharcParameters.gridParameters);
    float voxelSize = HashGridGetVoxelSize(level, sharcParameters.gridParameters);
    if (hitT <= voxelSize) return false;
    if (lobeType == 0) return true;
    float roughness2 = roughness * roughness;
    float denom = max(1.0 - roughness2, 1e-4);
    float coneSpread = 2.0 * hitT * sqrt(max(0.5 * roughness2 / denom, 0.0));
    return coneSpread > voxelSize;
}
#endif

#endif // VPT_PT_SHARC_GLSL
