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
    // Every surface these paths reach stands for the light that leaves it (the hit shaders sample block lights on
    // every bounce of them, see default.rchit).
    raySetCacheUpdate(mainRay, true);

    for (int b = 0; b < VPT_NUM_RAY_BOUNCES; ++b) {
        resetMainRay(mainRay, b);
        mainRay.throughput = vec3(1.0);
        mainRay.radiance = vec3(0.0);

        // No rain (WEATHER_MASK), as for the view's bounces (prepareSecondaryBase): its random streaks only
        // speckled the cache.
        uint mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | PARTICLE_MASK | CLOUD_MASK;
        if (b > 1) {
            mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | CLOUD_MASK;
        } else if (b == 0 && worldUBO.isFirstPerson > 0) {
            mask = WORLD_MASK | FISHING_BOBBER_MASK | PARTICLE_MASK | CLOUD_MASK;
        }
        float len = (b <= 1) ? 1000.0 : 384.0;

        vec3 tracedOrigin = mainRay.origin;
        vec3 tracedDirection = mainRay.direction;
        VPT_TRACE_PATH(rayFlags, mask, 1, 1, VPT_WORLD_NO_VOLUMETRIC_MISS_INDEX, mainRay.origin, 0.0001,
                       mainRay.direction, len);

        if (mainRay.hitT == INF_DISTANCE) {
            SharcUpdateMiss(sharcParameters, sharcState, mainRay.radiance);
            break;
        }

        MaterialInfo mat = rayLoadMaterial(mainRay);
        if (mat.transmission > 0.95) {
            mainRay.throughput = vec3(1.0);
            mainRay.radiance = vec3(0.0);
            if (rayShouldStop(mainRay)) { break; }
            continue;
        }

        SharcHitData sharcHitData;
        sharcHitData.positionWorld = tracedOrigin + tracedDirection * mainRay.hitT + vec3(worldUBO.cameraPos.xyz);
        sharcHitData.normalWorld = mainRay.normal;
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
