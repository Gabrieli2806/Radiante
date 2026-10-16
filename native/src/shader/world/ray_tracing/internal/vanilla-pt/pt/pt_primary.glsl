#ifndef VPT_PT_PRIMARY_GLSL
#define VPT_PT_PRIMARY_GLSL

// The camera's ray: path state set-up and the trace distance and masks per step. prepareSecondary sits here too,
// beside preparePrimary, as it did in world.rgen. The primary loop itself (through glass, water sides and mirrors up
// to the first surface that scatters, once tracePrimary) is the PT_STEP_PRIMARY half of ptRunSegment
// (pt_segment.glsl), so that it shares the one main-path trace with the bounces.

struct FirstHitPayload {
    bool hitSky;
    float surfaceDepth;
    float viewDepth;
    float firstHitLinearDepth;
    float tracedDistance;
    // Under water: how far the camera's ray travels before its first hit (INF_DISTANCE when it hits nothing). The
    // stretches after that are fogged by the hit and miss shaders, so the camera's fog covers only this one.
    float cameraWaterDistance;
    int startBounce;
    vec3 incomingDirection;
    vec3 outgoingDirection;
    vec3 normal;
    vec3 worldPos;
    // The side channels of the camera ray's last step (pt/pt_side_channels.glsl), for the first-hit outputs, which
    // run after the hand's ray and the glass reflection have been traced: the auxiliary pair of the first hit (water
    // flag and origin bias, Bedrock medium bits), and, when the view ended in the sky, the stars' visibility.
    vec2 aux;
    float starVisibility;
};

void initMainRay(vec3 origin, vec3 direction, uint seed, float coneSpread, out MainRay ray) {
    ray.origin = origin;
    ray.hitT = 0.0;
    ray.direction = direction;
    ray.seed = xxhash32(uvec3(gl_LaunchIDEXT.x, gl_LaunchIDEXT.y, seed));
    ray.radiance = vec3(0.0);
    ray.coneWidth = 0.0;
    ray.throughput = vec3(1.0);
    ray.coneSpread = coneSpread;
    ray.stateBits = 0u;
    raySetBounce(ray, 0u);
    raySetInsideBoat(ray, false);
    raySetStop(ray, false);
    raySetContinue(ray, false);
    raySetPassThrough(ray, false);
    raySetNoisy(ray, false);
    ray.prevScenePos = vec3(0.0);
    ray.hasPrevScenePos = 0u;
    ray.normal = vec3(0.0);
    raySetLobeType(ray, 0u);
    ray.directLightRadiance = vec3(0.0);
    ray.pad0 = 0u;
}

void resetMainRay(inout MainRay ray, int bounce) {
    raySetBounce(ray, uint(bounce));
    ray.hitT = 0.0;
    raySetStop(ray, false);
    raySetContinue(ray, false);
    raySetPassThrough(ray, false);
    raySetNoisy(ray, false);
    ray.prevScenePos = vec3(0.0);
    ray.hasPrevScenePos = 0u;
    ray.normal = vec3(0.0);
    raySetLobeType(ray, 0u);
    ray.directLightRadiance = vec3(0.0);
    ray.pad0 = 0u;
}

/** How bright night vision makes a surface, in the renderer's radiance units. */
#define NIGHT_VISION_AMBIENT 0.8

/** How far primary and first-bounce rays look for the world; further when far terrain is loaded. */
float worldTraceDistance() {
    // At least as far as a beacon beam is tall (2048 blocks, BeaconRenderer.MAX_RENDER_Y): with less its beam
    // ended in mid air, a sphere's radius from the camera, where vanilla shows it running on up out of sight.
    return max(2304.0, worldUBO.traceDistance);
}

/** computeCameraMotionVector for a point of the hand, which is seen through the hand's own field of view. */
vec2 handMotionVector(mat4 prevMVP, vec2 pixelCenter, vec3 position) {
    vec4 oldPos = prevMVP * vec4(position, 1.0);
    oldPos.xy /= oldPos.w;
    // Last frame's scale: sprinting and other field of view effects change it from frame to frame.
    float scale = lastWorldUBO.handFovScale;
    if (scale > 0.0) { oldPos.xy /= scale; }
    oldPos.xy = (oldPos.xy * 0.5 + 0.5) * vec2(gl_LaunchSizeEXT.xy);
    return oldPos.xy - pixelCenter;
}

void preparePrimary(int step, int isHand, out float len, out uint mask, out uint missId) {
    if (isHand == 0) {
        missId = step == 0 ? VPT_WORLD_MISS_INDEX : VPT_WORLD_NO_VOLUMETRIC_MISS_INDEX;
        if (step == 0) {
            len = worldTraceDistance();
            if (worldUBO.isFirstPerson > 0) {
                mask = WORLD_MASK | FISHING_BOBBER_MASK | WEATHER_MASK | PARTICLE_MASK | CLOUD_MASK | BOAT_WATER_MASK;
            } else {
                mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | WEATHER_MASK | PARTICLE_MASK | CLOUD_MASK |
                       BOAT_WATER_MASK;
            }
        } else if (step <= 1) {
            len = worldTraceDistance();
            mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | WEATHER_MASK | PARTICLE_MASK | CLOUD_MASK |
                   BOAT_WATER_MASK;
        } else {
            len = 384.0;
            mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | BOAT_WATER_MASK;
        }
    } else {
        if (step == 0) {
            len = 10.0;
            mask = HAND_MASK;
            missId = VPT_HAND_MISS_INDEX;
        } else if (step <= 1) {
            len = 384.0;
            mask = WORLD_MASK | FISHING_BOBBER_MASK | WEATHER_MASK | PARTICLE_MASK | CLOUD_MASK | BOAT_WATER_MASK;
            missId = step == 1 ? VPT_WORLD_MISS_INDEX : VPT_WORLD_NO_VOLUMETRIC_MISS_INDEX;
        } else {
            len = 384.0;
            mask = WORLD_MASK | FISHING_BOBBER_MASK | BOAT_WATER_MASK;
            missId = VPT_WORLD_NO_VOLUMETRIC_MISS_INDEX;
        }
    }
}

void prepareSecondary(int bounce, int isHand, out float len, out uint mask) {
    if (bounce == 0) {
        len = worldTraceDistance();
        if (worldUBO.isFirstPerson > 0) {
            // The held item has to be reachable by bounced light, otherwise a torch in the hand glows without
            // lighting anything around it in first person, while the same torch lights the scene in third person.
            mask = WORLD_MASK | HAND_MASK | FISHING_BOBBER_MASK | WEATHER_MASK | PARTICLE_MASK | CLOUD_MASK;
        } else {
            mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | WEATHER_MASK | PARTICLE_MASK | CLOUD_MASK;
        }
    } else if (bounce <= 1) {
        len = worldTraceDistance();
        if (isHand == 0) {
            mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | WEATHER_MASK | PARTICLE_MASK | CLOUD_MASK;
        } else {
            mask = WORLD_MASK | FISHING_BOBBER_MASK | WEATHER_MASK | PARTICLE_MASK | CLOUD_MASK;
        }
    } else {
        len = 384.0;
        mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK;
    }
}

#endif // VPT_PT_PRIMARY_GLSL
