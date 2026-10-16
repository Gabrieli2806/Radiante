#ifndef VPT_PT_SEGMENT_GLSL
#define VPT_PT_SEGMENT_GLSL

// A segment is one stretch of the pixel's path that used to be one call of a tracing function: the camera's ray
// through glass, water sides and mirrors up to its first surface (was tracePrimary, world or hand), or the bounces
// after a first hit (was traceSecondary: the glass reflection, the two branches of a transparent split, the plain
// bounces, each sample of the hand's averaged bounces). ptRunSegment runs either kind with the one ptStep() call
// (pt_trace.glsl), and world.rgen calls it from a single loop, so the compiled raygen holds one main-path trace.
//
// The two bodies are the old functions' bodies, statement for statement and in the same order, so every rand()
// draw and every write to the path state happens as before; only the loop around them is shared.

struct PtSegment {
    int mode;           // PT_STEP_PRIMARY or PT_STEP_BOUNCE
    int isHand;         // 1 for the hand's camera ray and the bounces after a hand hit
    int startBounce;    // PT_STEP_BOUNCE: the first bounce index
    vec3 orgDirection;  // PT_STEP_PRIMARY: the camera ray's direction (the world's, or the hand's own frustum)
};

PtSegment ptPrimarySegment(int isHand, vec3 orgDirection) {
    return PtSegment(PT_STEP_PRIMARY, isHand, 0, orgDirection);
}

PtSegment ptBounceSegment(int startBounce, int isHand) {
    return PtSegment(PT_STEP_BOUNCE, isHand, startBounce, vec3(0.0));
}

void ptRunSegment(PtSegment seg,
                  vec3 eyePos,
                  uint rayFlags,
                  inout MainRay rayState,
                  out FirstHitPayload firstHit,
#if SHARC_QUERY && USE_SHARC
                  SharcParameters sharcParameters,
                  inout int sharcDebugBounceCount,
                  inout bool sharcCacheHit,
#endif
                  out bool hasSecondHit,
                  out float secondHitDistance) {
    bool primary = seg.mode == PT_STEP_PRIMARY;

    // Camera ray (PT_STEP_PRIMARY) state.
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
    firstHit.aux = vec2(0.0);
    firstHit.starVisibility = 0.0;
    bool surfaceDepthSampled = false;
    bool cameraPath = true;
    // Ended in the sky: the old tracePrimary returned there, before the view depth at the end.
    bool primaryHitSky = false;

    // Bounce (PT_STEP_BOUNCE) state.
    bool isPrimaryCameraPath = true;
    hasSecondHit = false;
    secondHitDistance = 0.0;
    // Bounces off a mirror or through clear glass do not count against the budget, up to MIRROR_EXTRA_BOUNCES more:
    // two mirrors facing each other used up the few bounces in the first reflections, and the mirror seen inside the
    // mirror came out black.
    int extraBounces = 0;
    bool viewChain = false;
    int budget = VPT_NUM_RAY_BOUNCES;
    // Bedrock RTX's budget everywhere (VPT_BEDROCK_ONE_BOUNCE): one bounce traced past the first surface, the rest of
    // the light from the radiance cache where it lands (below). Mirrors and clear glass still get their extra bounces.
    if (VPT_BEDROCK_ONE_BOUNCE != 0) { budget = min(budget, seg.startBounce + 1); }
#if SHARC_QUERY && PT_SHARC_PRESHADE
    // Pre-shade cache order: the roughness of the last surface the path scattered from (not passed through), the
    // cone its lobe spreads in. A segment starts from the surface shaded last.
    float arrivingRoughness = g_lastMaterial.roughness;
#endif

    // i is the primary step, or the absolute bounce index b.
    for (int i = primary ? 0 : seg.startBounce;; i++) {
        StepParams params;
        uint leavingLobeType = 0u;
        if (primary) {
            if (i >= VPT_PRIMARY_TRACE_STEP_LIMIT) break;
            resetMainRay(rayState, i);
            raySetCameraPath(rayState, cameraPath);
            // What the glass passed through so far lets through: the air past it is seen through it too.
            g_passThroughTint = rayState.throughput;
            g_passThroughRadiance = rayState.radiance;
            g_passThroughDistance = firstHit.tracedDistance;

            float len;
            uint mask;
            uint missId;
            preparePrimary(i, seg.isHand, len, mask, missId);
            // In first person the player's own model stays out of the view for as long as the view runs straight
            // on. Past a cloud's face, a pane or a raindrop it was looked for again, and whatever of it lay beyond
            // showed: rising into a cloud, with its underside between the eyes and the body, the body from within,
            // black. Only a view turned back by a mirror shows the player.
            if (seg.isHand == 0 && i > 0 && worldUBO.isFirstPerson > 0 &&
                dot(rayState.direction, seg.orgDirection) > 0.9999) {
                mask &= ~PLAYER_MASK;
            }
            params = StepParams(PT_STEP_PRIMARY, rayFlags, mask, missId, 0.0001, len, false, 0u);
        } else {
            if (i >= budget + extraBounces) break;
            if (rayShouldStop(rayState)) break;

            uint mask;
            float len;
            leavingLobeType = rayLobeType(rayState);
            uint firstBounceLobeType = i == seg.startBounce ? leavingLobeType : 0u;
            // Still what the camera sees, off or through one mirror-like surface after another: a window, then the
            // water behind it. It ends at the first surface the ray scatters from.
            viewChain = (i == seg.startBounce || viewChain) && leavingLobeType != 0u;
            resetMainRay(rayState, i);
            prepareSecondary(i, seg.isHand, len, mask);
            // What such a ray ends in is seen, not gathered as light; see the miss shader and the water medium.
            if (viewChain) { rayState.pad0 = RAY_VIEW_BOUNCE_MARK; }

            uint missId = VPT_WORLD_NO_VOLUMETRIC_MISS_INDEX;
            bool allowIndirectCloudMiss =
                (firstBounceLobeType == 1u || firstBounceLobeType == 2u) &&
                rayUseIndirectVolumetricCloud(rayState);
            if (i == seg.startBounce && VPT_CAPTURE_VOLUMETRIC_CLOUD_INDIRECT != 0 && allowIndirectCloudMiss) {
                missId = VPT_WORLD_MISS_INDEX;
            }
            // The first bounce (reflections and refraction seen straight from the camera, water among them) is
            // traced as usual; the later, scattered ones are reordered.
            // Known issue: with reordering, water can come out white. Experimental, off by default.
#if PT_SER_REORDER_ALL_BOUNCES != 0
            // Experiment (world.rgen): every bounce is reordered.
            bool reorder = true;
#else
            bool reorder = i > seg.startBounce && !rayInWater(rayState) && !rayIgnoreWaterSelf(rayState);
#endif
            params = StepParams(PT_STEP_BOUNCE, VPT_RAY_FLAGS, mask, missId, 0.001, len, reorder, leavingLobeType);
        }
#if SHARC_QUERY && PT_SHARC_PRESHADE
        // Pre-shade cache order: whether this step may take the cache before shading, by the legacy rule's own terms
        // (below): past a surface that scattered, or the first diffuse bounce with one bounce. The camera's steps
        // never ask.
        ptSharcPreShadeReset();
        if (!primary) {
            bool preShadeFirstBounce = VPT_BEDROCK_ONE_BOUNCE != 0 && i == seg.startBounce && leavingLobeType == 0u;
            g_sharcPreShade.query = !isPrimaryCameraPath || preShadeFirstBounce;
            g_sharcPreShade.deepBounce = (VPT_CACHE_DEEP_BOUNCES != 0 && i > seg.startBounce) || preShadeFirstBounce;
            g_sharcPreShade.arrivingRoughness = arrivingRoughness;
        }
#endif

        vec3 tracedOrigin = rayState.origin;
        vec3 tracedDirection = rayState.direction;
        ptStep(rayState, params);

        if (primary) {
            if (i == 0) {
                firstHit.cameraWaterDistance = rayState.hitT;
            }
            // The camera ray's state after its last step is what the first-hit outputs read; the pair is kept for
            // them from every step, the last one counts.
            firstHit.aux = g_step.aux;
            // The first reflection a pass-through surface asks for (glass, the side of water) is traced later.
            PtReflectRequest request = g_step.reflectRequest;
            if (rayIsPassThrough(rayState) && !g_glassReflection &&
                max(request.weight.r, max(request.weight.g, request.weight.b)) > 1e-4 &&
                dot(request.dir, request.dir) > 0.5) {
                g_glassReflection = true;
                g_glassReflectionOrigin = tracedOrigin + tracedDirection * rayState.hitT;
                g_glassReflectionDirection = normalize(request.dir);
                g_glassReflectionWeight = request.weight;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
                g_glassReflectionMedium = request.mediumBits;
#endif
            }
            cameraPath = cameraPath && rayIsPassThrough(rayState);
            raySetCameraPath(rayState, false);

            if (!surfaceDepthSampled && !rayIsPassThrough(rayState)) {
                firstHit.surfaceDepth = firstHit.tracedDistance + rayState.hitT;
                surfaceDepthSampled = true;
                if (rayState.hitT != INF_DISTANCE) {
                    vec3 hitPos = eyePos + seg.orgDirection * (firstHit.tracedDistance + rayState.hitT);
                    hitPos -= worldUBO.cameraViewMatInv[3].xyz;
                    firstHit.firstHitLinearDepth = -(mat3(worldUBO.cameraEffectedViewMat) * hitPos).z;
                }
            }

            if (rayState.hitT == INF_DISTANCE) {
                firstHit.hitSky = true;
                firstHit.starVisibility = g_step.starVisibility;
                firstHit.startBounce = i + 1;
                primaryHitSky = true;
                break;
            }

            firstHit.tracedDistance += rayState.hitT;
            firstHit.startBounce = i + 1;

            if (rayShouldContinue(rayState)) {
                // Out of steps still going (a room of mirrors facing each other, glass behind glass): the view ends
                // there, dark, as a surface at that distance. Left as it was it counted as open sky, and the stars
                // drawn over the sky afterwards showed in the mirrors at night.
                if (i == VPT_PRIMARY_TRACE_STEP_LIMIT - 1 && !surfaceDepthSampled) {
                    firstHit.surfaceDepth = firstHit.tracedDistance;
                    surfaceDepthSampled = true;
                    vec3 endPos =
                        eyePos + seg.orgDirection * firstHit.tracedDistance - worldUBO.cameraViewMatInv[3].xyz;
                    firstHit.firstHitLinearDepth = -(mat3(worldUBO.cameraEffectedViewMat) * endPos).z;
                }
                continue;
            }

            firstHit.incomingDirection = tracedDirection;
            firstHit.outgoingDirection = rayState.direction;
            firstHit.normal = rayState.normal;
            firstHit.worldPos = tracedOrigin + tracedDirection * rayState.hitT;
            break;
        }

        raySetIgnoreWaterSelf(rayState, false);
        // A Bedrock glass refraction branch starts as a camera view (initTransparent): the surface behind the glass
        // is shaded as the camera sees it. Past that surface the path is an ordinary bounce, as the camera ray's own
        // rule has it (the bit stays only while the view passes straight through); left set, every later bounce of
        // the branch counted as the camera's view and ReSTIR overwrote the pixel's reservoir from deep bounces.
        raySetCameraPath(rayState, rayCameraPath(rayState) && rayIsPassThrough(rayState));

        if (i == seg.startBounce) {
            secondHitDistance = rayState.hitT;
            hasSecondHit = rayState.hitT != INF_DISTANCE;
        }

        if (rayState.hitT != INF_DISTANCE) {
            MaterialInfo hitMat = g_lastMaterial;
            vec3 hitWorldPos = tracedOrigin + tracedDirection * rayState.hitT;
#if SHARC_QUERY && USE_SHARC
#    if PT_SHARC_PRESHADE
            // Pre-shade cache order: the cache already answered for this hit, before it was shaded
            // (pt/pt_sharc_preshade.glsl); there is nothing to ask after.
            if (g_sharcPreShade.cacheHit) {
                sharcCacheHit = true;
                break;
            }
#    else
            vec3 positionWorld = hitWorldPos + vec3(worldUBO.cameraPos.xyz);
            // With one bounce (VPT_BEDROCK_ONE_BOUNCE) a diffuse bounce off the first surface already lands where the
            // camera does not look: the cache answers there, as Bedrock's irradiance cache does.
            bool cacheFirstBounce = VPT_BEDROCK_ONE_BOUNCE != 0 && i == seg.startBounce && leavingLobeType == 0u;
            // Not where the ray only passed through (glass crossed by light on its way): nothing is cached there,
            // and what another surface in the same cell holds is not what lies behind the glass.
            if (!rayIsPassThrough(rayState) &&
                sharcAllowQuery(isPrimaryCameraPath && !cacheFirstBounce,
                                (VPT_CACHE_DEEP_BOUNCES != 0 && i > seg.startBounce) || cacheFirstBounce,
                                rayState.hitT, hitMat.roughness, rayLobeType(rayState), sharcParameters,
                                positionWorld)) {
                SharcHitData sharcHitData;
                sharcHitData.positionWorld = positionWorld;
                sharcHitData.normalWorld = rayState.normal;
                vec3 cachedRadiance = vec3(0.0);
                if (SharcGetCachedRadiance(sharcParameters, sharcHitData, cachedRadiance, false)) {
                    sharcCacheHit = true;
                    rayState.radiance += cachedRadiance * rayState.throughput;
                    break;
                }
            }
#    endif
            sharcDebugBounceCount = max(sharcDebugBounceCount, i + 1);
#endif
            // Seen straight through (the side of a waterfall the view crosses, see pt/pt_shade_default.glsl): not a
            // bounce.
            bool passedThrough = rayIsPassThrough(rayState);
            bool isPerfectMirrorOrTransparent =
                passedThrough || ((hitMat.roughness < 0.02) && (rayLobeType(rayState) != 0u));
            if (!isPerfectMirrorOrTransparent) { isPrimaryCameraPath = false; }
#if SHARC_QUERY && PT_SHARC_PRESHADE
            if (!passedThrough) { arrivingRoughness = hitMat.roughness; }
#endif
            if ((passedThrough || (hitMat.roughness < 0.08 && rayLobeType(rayState) != 0u)) &&
                extraBounces < MIRROR_EXTRA_BOUNCES) {
                extraBounces++;
            }
        }

        // A chain of mirrors seen from the camera is not cut short at random: the reflection inside a reflection
        // would come out speckled. Its length is bounded by the mirror bounces allowed.
        if (i > 2 && !isPrimaryCameraPath) {
            float maxComponent = max(rayState.throughput.r, max(rayState.throughput.g, rayState.throughput.b));
            float rrProbability = min(maxComponent, 0.9);
            if (rand(rayState.seed) > rrProbability) { break; }
            rayState.throughput /= rrProbability;
        }
    }

    if (primary) {
        if (!primaryHitSky) {
            vec3 virtualOrigin = eyePos + seg.orgDirection * firstHit.tracedDistance;
            firstHit.viewDepth = -(worldUBO.cameraEffectedViewMat * vec4(virtualOrigin, 1.0)).z;
        }
    } else {
        // The bounce loop leaves the end of its path in mainRay as well: the code after a segment reads it (the glass
        // reflection's radiance, the split's last branch state).
        mainRay = rayState;
    }
}

#endif // VPT_PT_SEGMENT_GLSL
