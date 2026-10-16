#ifndef VPT_PT_SHARC_UPDATE_GLSL
#define VPT_PT_SHARC_UPDATE_GLSL

// SHaRC radiance cache, update side: the sparse paths that fill the cache (the SHARC_UPDATE raygen variant). It
// needs the path state helpers of pt_primary.glsl, so it is included after them, unlike the query side
// (pt_sharc.glsl), which the bounce loop needs earlier. Moved verbatim out of world.rgen.

#if USE_SHARC && SHARC_UPDATE
void traceSharcUpdate(
    ivec2 pixel, vec2 resolution, vec3 origin, vec3 direction, float fovY, float fovX, uint rayFlags) {
    SharcParameters sharcParameters = makeSharcParameters();
    SharcState sharcState;
    SharcInit(sharcState);

    initMainRay(origin, direction, worldUBO.seed ^ sharcConfig.frameIndex, coneSpreadFromFov(fovY, fovX, resolution),
                    mainRay);
    // Every surface these paths reach stands for the light that leaves it (the shading samples block lights on
    // every bounce of them, see pt/pt_shade_default.glsl).
    raySetCacheUpdate(mainRay, true);

    for (int b = 0; b < VPT_NUM_RAY_BOUNCES; ++b) {
        resetMainRay(mainRay, b);
        mainRay.throughput = vec3(1.0);
        mainRay.radiance = vec3(0.0);

        uint mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | WEATHER_MASK | PARTICLE_MASK | CLOUD_MASK;
        if (b > 1) {
            mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | CLOUD_MASK;
        } else if (b == 0 && worldUBO.isFirstPerson > 0) {
            mask = WORLD_MASK | FISHING_BOBBER_MASK | WEATHER_MASK | PARTICLE_MASK | CLOUD_MASK;
        }
        float len = (b <= 1) ? 1000.0 : 384.0;

        vec3 tracedOrigin = mainRay.origin;
        vec3 tracedDirection = mainRay.direction;
        // Through ptStep like every other main-path trace (pt_trace.glsl), so recorded hits get shaded here too.
        ptStep(mainRay, StepParams(PT_STEP_CACHE_UPDATE, rayFlags, mask, VPT_WORLD_NO_VOLUMETRIC_MISS_INDEX, 0.0001,
                                   len, false, 0u));

        if (mainRay.hitT == INF_DISTANCE) {
            SharcUpdateMiss(sharcParameters, sharcState, mainRay.radiance);
            break;
        }

        MaterialInfo mat = g_lastMaterial;
        if (mat.transmission > 0.95) {
            mainRay.throughput = vec3(1.0);
            mainRay.radiance = vec3(0.0);
            if (rayShouldStop(mainRay)) { break; }
            continue;
        }

        SharcHitData sharcHitData;
        sharcHitData.positionWorld = tracedOrigin + tracedDirection * mainRay.hitT + vec3(worldUBO.cameraPos.xyz);
#if PT_SHARC_PRESHADE
        // Filed under the triangle's own normal, the one the pre-shade lookups ask with (they run before the shading
        // normal exists): a cell stored under one normal is not found under another.
        sharcHitData.normalWorld = ptSharcHitNormal();
#else
        sharcHitData.normalWorld = mainRay.normal;
#endif
        bool accepted = SharcUpdateHit(sharcParameters, sharcState, sharcHitData, mainRay.radiance, rand(mainRay.seed));
        if (!accepted) { break; }

        SharcSetThroughput(sharcState, max(mainRay.throughput, vec3(0.0)));
        mainRay.throughput = vec3(1.0);
        mainRay.radiance = vec3(0.0);
        if (rayShouldStop(mainRay)) { break; }
    }
}
#endif

#endif // VPT_PT_SHARC_UPDATE_GLSL
