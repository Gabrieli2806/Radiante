#ifndef VPT_PT_PRIMARY_GLSL
#define VPT_PT_PRIMARY_GLSL

// The camera's ray: path state set-up, the trace distance and masks per step, and the primary loop through glass,
// water sides and mirrors up to the first surface that scatters (tracePrimary). prepareSecondary sits here too,
// beside preparePrimary, as it did in world.rgen. Moved verbatim out of world.rgen.

// Whether a pack moves where the reflections of the surface the camera sees start (hooks/ray_origins.glsl).
#include "hooks/ray_origins.glsl"

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
#ifndef RADIANTE_HOOK_RAY_ORIGINS_DEFAULT
    // Where the pack starts that surface's reflection (worldPos when it does not move it), and the view of that
    // point from where the ray that met the surface came from.
    vec3 reflectionPos;
    vec3 reflectionIncoming;
#endif
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
    raySetSkipFog(ray, false);
    ray.prevScenePos = vec3(0.0);
    ray.hasPrevScenePos = 0u;
    ray.normal = vec3(0.0);
    raySetLobeType(ray, 0u);
    ray.directLightRadiance = vec3(0.0);
    ray.pad0 = 0u;
}

void resetMainRay(inout MainRay ray, int bounce) {
    // A ray that went on through a cloud is still on its way; one that scattered off a surface left the cloud behind.
    if (!rayShouldContinue(ray)) { raySetCrossedCloud(ray, false); }
    raySetBounce(ray, uint(bounce));
    ray.hitT = 0.0;
    raySetStop(ray, false);
    raySetContinue(ray, false);
    raySetPassThrough(ray, false);
    raySetNoisy(ray, false);
    raySetSkipFog(ray, false);
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

void prepareSecondaryBase(int bounce, int isHand, out float len, out uint mask) {
    // No rain on bounced rays (WEATHER_MASK): a streak is kept or skipped at random each frame by the any-hit, so a
    // bounce that met one carried a different glint every frame, and reflections and indirect light in rain came out
    // speckled. The streaks also cost an any-hit call for every bounce through a storm. The camera still sees them.
    if (bounce == 0) {
        len = worldTraceDistance();
        if (worldUBO.isFirstPerson > 0) {
            // The held item has to be reachable by bounced light, otherwise a torch in the hand glows without
            // lighting anything around it in first person, while the same torch lights the scene in third person.
            mask = WORLD_MASK | HAND_MASK | FISHING_BOBBER_MASK | PARTICLE_MASK | CLOUD_MASK;
        } else {
            mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | PARTICLE_MASK | CLOUD_MASK;
        }
    } else if (bounce <= 1) {
        len = worldTraceDistance();
        if (isHand == 0) {
            mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK | PARTICLE_MASK | CLOUD_MASK;
        } else {
            mask = WORLD_MASK | FISHING_BOBBER_MASK | PARTICLE_MASK | CLOUD_MASK;
        }
    } else {
        len = 384.0;
        mask = WORLD_MASK | PLAYER_MASK | FISHING_BOBBER_MASK;
    }
}

/** Rays bounced off or bent by a surface. In first person the player's own body is left out of what they find (it
 *  casts shadows, see g_shadowPlayerMask), except along a mirror-like view, which pt_bounce.glsl lets see it. */
void prepareSecondary(int bounce, int isHand, out float len, out uint mask) {
    prepareSecondaryBase(bounce, isHand, len, mask);
    if (worldUBO.isFirstPerson > 0) { mask &= ~PLAYER_MASK; }
}

void tracePrimary(vec3 eyePos,
                  vec3 orgDirection,
                  uint rayFlags,
                  int isHand,
                  inout MainRay rayState,
                  out FirstHitPayload firstHit) {
    firstHit.hitSky = false;
    firstHit.surfaceDepth = INF_DISTANCE;
    firstHit.viewDepth = INF_DISTANCE;
    firstHit.firstHitLinearDepth = INF_DISTANCE;
    firstHit.tracedDistance = 0.0;
    firstHit.cameraWaterDistance = INF_DISTANCE;
    firstHit.startBounce = 0;
    firstHit.incomingDirection = vec3(0.0);
    firstHit.outgoingDirection = vec3(0.0);
    firstHit.normal = vec3(0.0);
    firstHit.worldPos = vec3(0.0);
#ifndef RADIANTE_HOOK_RAY_ORIGINS_DEFAULT
    firstHit.reflectionPos = vec3(0.0);
    firstHit.reflectionIncoming = vec3(0.0);
#endif

    bool surfaceDepthSampled = false;
    bool cameraPath = true;
    for (int step = 0; step < VPT_PRIMARY_TRACE_STEP_LIMIT; ++step) {
        resetMainRay(rayState, step);
        raySetCameraPath(rayState, cameraPath);
        // What the glass passed through so far lets through: the air past it is seen through it too.
        g_passThroughTint = rayState.throughput;
        g_passThroughRadiance = rayState.radiance;
        g_passThroughDistance = firstHit.tracedDistance;

        float len;
        uint mask;
        uint missId;
        preparePrimary(step, isHand, len, mask, missId);
        // In first person the player's own model stays out of the view for as long as the view runs straight on.
        // Past a cloud's face, a pane or a raindrop it was looked for again, and whatever of it lay beyond showed:
        // rising into a cloud, with its underside between the eyes and the body, the body from within, black.
        // Only a view turned back by a mirror shows the player.
        if (isHand == 0 && step > 0 && worldUBO.isFirstPerson > 0 &&
            dot(rayState.direction, orgDirection) > 0.9999) {
            mask &= ~PLAYER_MASK;
        }

        vec3 tracedOrigin = rayState.origin;
        vec3 tracedDirection = rayState.direction;
        mainRay = rayState;
        VPT_TRACE_PATH(rayFlags, mask, 1, 1, missId, mainRay.origin, 0.0001, mainRay.direction, len);
        rayState = mainRay;
        if (step == 0) {
            firstHit.cameraWaterDistance = rayState.hitT;
        }
        if (rayIsPassThrough(rayState) && !g_glassReflection &&
            max(rayState.directLightRadiance.r, max(rayState.directLightRadiance.g, rayState.directLightRadiance.b)) >
                1e-4 && dot(rayState.normal, rayState.normal) > 0.5) {
            g_glassReflection = true;
            g_glassReflectionOrigin = tracedOrigin + tracedDirection * rayState.hitT;
            g_glassReflectionDirection = normalize(rayState.normal);
            g_glassReflectionWeight = rayState.directLightRadiance;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
            g_glassReflectionMedium = uint(round(rayLoadAux(rayState).y));
#endif
        }
        cameraPath = cameraPath && rayIsPassThrough(rayState);
        raySetCameraPath(rayState, false);

        if (!surfaceDepthSampled && !rayIsPassThrough(rayState)) {
            firstHit.surfaceDepth = firstHit.tracedDistance + rayState.hitT;
            surfaceDepthSampled = true;
            if (rayState.hitT != INF_DISTANCE) {
                vec3 hitPos = eyePos + orgDirection * (firstHit.tracedDistance + rayState.hitT);
                hitPos -= worldUBO.cameraViewMatInv[3].xyz;
                firstHit.firstHitLinearDepth = -(mat3(worldUBO.cameraEffectedViewMat) * hitPos).z;
            }
        }

        if (rayState.hitT == INF_DISTANCE) {
            firstHit.hitSky = true;
            firstHit.startBounce = step + 1;
            return;
        }

        firstHit.tracedDistance += rayState.hitT;
        firstHit.startBounce = step + 1;

        if (rayShouldContinue(rayState)) {
            // Out of steps still going (a room of mirrors facing each other, glass behind glass): the view ends
            // there, dark, as a surface at that distance. Left as it was it counted as open sky, and the stars
            // drawn over the sky afterwards showed in the mirrors at night.
            if (step == VPT_PRIMARY_TRACE_STEP_LIMIT - 1 && !surfaceDepthSampled) {
                firstHit.surfaceDepth = firstHit.tracedDistance;
                surfaceDepthSampled = true;
                vec3 endPos = eyePos + orgDirection * firstHit.tracedDistance - worldUBO.cameraViewMatInv[3].xyz;
                firstHit.firstHitLinearDepth = -(mat3(worldUBO.cameraEffectedViewMat) * endPos).z;
            }
            continue;
        }

        firstHit.incomingDirection = tracedDirection;
        firstHit.outgoingDirection = rayState.direction;
        firstHit.normal = rayState.normal;
        firstHit.worldPos = tracedOrigin + tracedDirection * rayState.hitT;
#ifndef RADIANTE_HOOK_RAY_ORIGINS_DEFAULT
        // The offset the hit shader left in the material image's fourth layer (pt_shade_default.glsl). Only the
        // default shading writes it: a hit of another group (clouds, the End portal) leaves an older one there,
        // which is never more than a block away for a texel; anything further is not a reflection origin and is
        // dropped.
        memoryBarrierImage();
        vec3 reflectionOffset = imageLoad(rayMaterialStateImage, rayMaterialStateCoord(3)).xyz;
        if (!(dot(reflectionOffset, reflectionOffset) <= 1.0)) { reflectionOffset = vec3(0.0); }
        firstHit.reflectionPos = firstHit.worldPos + reflectionOffset;
        vec3 toReflectionPos = firstHit.reflectionPos - tracedOrigin;
        firstHit.reflectionIncoming =
            dot(toReflectionPos, toReflectionPos) > 1e-12 ? normalize(toReflectionPos) : tracedDirection;
#endif
        break;
    }

    vec3 virtualOrigin = eyePos + orgDirection * firstHit.tracedDistance;
    firstHit.viewDepth = -(worldUBO.cameraEffectedViewMat * vec4(virtualOrigin, 1.0)).z;
}

#endif // VPT_PT_PRIMARY_GLSL
