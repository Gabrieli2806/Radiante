#ifndef VPT_PT_BOUNCE_GLSL
#define VPT_PT_BOUNCE_GLSL

// The bounces after the first hit: the split into reflection and refraction at transparent surfaces, the bounce loop
// with the SHaRC query and Russian roulette (traceSecondary), and the averaged variant for the hand. Moved verbatim
// out of world.rgen.

bool initTransparent(MainRay rayState,
                     MaterialInfo mat,
                     vec3 incomingDirection,
                     vec3 normal,
                     vec3 worldPos,
                     uint branchSalt,
                     bool isReflection,
                     out MainRay branchRay) {
    vec3 incident = normalize(incomingDirection);
    vec3 viewDir = -incident;
    float eta = dot(viewDir, normal) > 0.0 ? (1.0 / mat.ior) : mat.ior;
    vec2 branchAux = rayLoadAux(rayState);
    bool isWaterSurface = branchAux.y > 0.5;
    // Bedrock compatibility: a glass block is seen straight through, undistorted, with a Fresnel reflection of the
    // world on its face (as Bedrock RTX shows it); the view behind goes on as a camera view, through the far face
    // as through a pane (default.rchit). Water keeps its refraction.
    bool bedrockGlass = VPT_FRESNEL_MODEL != 0 && !isWaterSurface;
    if (bedrockGlass) { eta = 1.0 / mat.ior; }
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    if (isWaterSurface) {
        eta = branchAux.y > 1.5 ? mat.ior : 1.0 / mat.ior;
        if (dot(incident, normal) > 0.0) { normal = -normal; }
    }
#endif
    float fresnel = clamp(vptInterfaceFresnel(abs(dot(viewDir, normal)), eta), 0.0, 1.0);

    vec3 branchDirection = isReflection ? reflect(incident, normal) :
                           (bedrockGlass ? incident : refract(incident, normal, eta));
    if (dot(branchDirection, branchDirection) <= 1e-6) { return false; }
    branchDirection = normalize(branchDirection);

    // Water is coloured by its medium on the way through (see default.rchit), so its surface lets light through
    // untinted; glass takes its colour here.
    vec3 refractionTint = isWaterSurface && waterMediumActive() ?
                              vec3(1.0) :
                              clamp(viewGlassTint(mat.albedoValue.rgb, mat.albedoValue.a), vec3(0.0), vec3(1.0));
    vec3 branchThroughput = isReflection ? vec3(fresnel) : refractionTint * (1.0 - fresnel);
    if (max(branchThroughput.r, max(branchThroughput.g, branchThroughput.b)) <= 1e-6) { return false; }
    float originBias = isWaterSurface ? max(branchAux.x, 0.0002) : 0.0001;

    branchRay = rayState;
    branchRay.origin = worldPos + (dot(branchDirection, normal) > 0.0 ? normal : -normal) * originBias;
    branchRay.direction = branchDirection;
    branchRay.radiance = vec3(0.0);
    // Seen through stained glass the water is coloured by it like everything else behind the pane. Starting the two
    // branches afresh left ponds and the sea at full brightness in a view the glass had darkened.
    branchRay.throughput = branchThroughput * clamp(g_passThroughTint, vec3(0.0), vec3(1.0));
    branchRay.hitT = 0.0;
    branchRay.seed = xxhash32(uvec3(rayState.seed, branchSalt, worldUBO.seed));
    raySetStop(branchRay, false);
    raySetContinue(branchRay, false);
    raySetNoisy(branchRay, false);
    branchRay.prevScenePos = vec3(0.0);
    branchRay.hasPrevScenePos = 0u;
    branchRay.normal = vec3(0.0);
    raySetLobeType(branchRay, isReflection ? 1u : 2u);
    if (bedrockGlass && !isReflection) { raySetCameraPath(branchRay, true); }
    raySetIgnoreWaterSelf(branchRay, isWaterSurface);
    // The same rule as a water hit's own bounce (see default.rchit): off the top by direction, through a side by swap.
    if (isWaterSurface) {
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        bool incomingWater = branchAux.y > 1.5;
        raySetInWater(branchRay, isReflection ? incomingWater : !incomingWater);
#else
        if (abs(normal.y) > 0.5) {
            raySetInWater(branchRay, branchDirection.y < 0.0);
        } else if (!isReflection) {
            raySetInWater(branchRay, !rayInWater(rayState));
        }
#endif
    }
    raySetIndirectVolumetricCloud(
        branchRay, vptShouldCaptureIndirectVolumetricCloud(isReflection ? 1u : 2u, mat.roughness));
    branchRay.directLightRadiance = vec3(0.0);
    return true;
}

#ifndef VPT_MIRROR_EXTRA_BOUNCES
#    define VPT_MIRROR_EXTRA_BOUNCES 8
#endif
// A macro rather than a const: the setting changes live, read from the world uniform, and a global const needs a
// value known when the shader is compiled.
#define MIRROR_EXTRA_BOUNCES VPT_MIRROR_EXTRA_BOUNCES

#ifndef VPT_EXPOSURE_DAY_SCALE
#    define VPT_EXPOSURE_DAY_SCALE 0.48
#endif
#ifndef VPT_EXPOSURE_NIGHT_SCALE
#    define VPT_EXPOSURE_NIGHT_SCALE 0.65
#endif
#ifndef VPT_EXPOSURE_DAY_MAX
#    define VPT_EXPOSURE_DAY_MAX 2.5
#endif
#ifndef VPT_BEDROCK_ONE_BOUNCE
#    define VPT_BEDROCK_ONE_BOUNCE 0
#endif
#ifndef VPT_TRANSPARENT_CHECKERBOARD
#    define VPT_TRANSPARENT_CHECKERBOARD 0
#endif

// One of the two cells of a checkerboard that swaps every frame: half the pixels do a piece of work this frame, the
// other half the next, as Bedrock RTX splits its frame. The denoiser fills in the half left out.
bool checkerboardCell(ivec2 pixel) {
    return ((pixel.x + pixel.y + int(worldUBO.frameCounter & 1u)) & 1) == 0;
}

void traceSecondary(inout MainRay rayState,
                   int startBounce,
                   int isHand,
#if SHARC_QUERY && USE_SHARC
                   SharcParameters sharcParameters,
                   inout int sharcDebugBounceCount,
                   inout bool sharcCacheHit,
#endif
                   out bool hasSecondHit,
                   out float secondHitDistance) {
    bool isPrimaryCameraPath = true;
    hasSecondHit = false;
    secondHitDistance = 0.0;

    // Bounces off a mirror or through clear glass do not count against the budget, up to MIRROR_EXTRA_BOUNCES more:
    // two mirrors facing each other used up the few bounces in the first reflections, and the mirror seen inside the
    // mirror came out black.
    int extraBounces = 0;
    bool viewChain = false;
    bool clearView = false;
    // Counted from the surface the camera first stops at: each glass face the view crossed on its way there is one
    // step of startBounce, and a wall seen through two blocks of glass had used up the whole budget before its first
    // bounce and came out black.
    int budget = VPT_NUM_RAY_BOUNCES + max(startBounce - 1, 0);
    // Bedrock RTX's budget everywhere (VPT_BEDROCK_ONE_BOUNCE): one bounce traced past the first surface, the rest of
    // the light from the radiance cache where it lands (below). Mirrors and clear glass still get their extra bounces.
    if (VPT_BEDROCK_ONE_BOUNCE != 0) { budget = min(budget, startBounce + 1); }
    for (int b = startBounce; b < budget + extraBounces; b++) {
        if (rayShouldStop(rayState)) break;

        uint mask;
        float len;
        uint leavingLobeType = rayLobeType(rayState);
        uint firstBounceLobeType = b == startBounce ? leavingLobeType : 0u;
        // Still what the camera sees, off or through one mirror-like surface after another: a window, then the
        // water behind it. It ends at the first surface the ray scatters from.
        viewChain = (b == startBounce || viewChain) && leavingLobeType != 0u;
        // The surface this ray leaves is one the camera sees clearly: the first hit, or one seen through water or
        // glass or in a smooth mirror (the floor of a pool, a wall in a mirror) - not one met by a glossy bounce off a
        // rough surface, which is light gathered for that surface (see the extra bounce below).
        bool fromSeenSurface = b == startBounce || clearView;
        clearView = fromSeenSurface &&
                    (leavingLobeType == 2u || (leavingLobeType == 1u && rayLoadMaterial(rayState).roughness < 0.08));
        resetMainRay(rayState, b);
        prepareSecondary(b, isHand, len, mask);
        // In first person a mirror-like view (wet ground, a puddle, water, a window) shows the player, as it does in
        // third person: their body is in the scene only while First Person Shadow is on. Light gathered by rough
        // bounces still leaves it out, so the body seen from within does not darken what is around the camera.
        if (viewChain && isHand == 0 && worldUBO.isFirstPerson > 0) { mask |= PLAYER_MASK; }
        // What such a ray ends in is seen, not gathered as light; see the miss shader and the water medium.
        if (viewChain) { rayState.pad0 = RAY_VIEW_BOUNCE_MARK; }

        vec3 tracedOrigin = rayState.origin;
        vec3 tracedDirection = rayState.direction;
        mainRay = rayState;
        uint missId = VPT_WORLD_NO_VOLUMETRIC_MISS_INDEX;
        bool allowIndirectCloudMiss =
            (firstBounceLobeType == 1u || firstBounceLobeType == 2u) &&
            rayUseIndirectVolumetricCloud(rayState);
        if (b == startBounce && VPT_CAPTURE_VOLUMETRIC_CLOUD_INDIRECT != 0 && allowIndirectCloudMiss) {
            missId = VPT_WORLD_MISS_INDEX;
        }
        RADIANTE_DEBUG_COUNT_BOUNCE();
        // The first bounce (reflections and refraction seen straight from the camera, water among them) is traced as
        // usual; the later, scattered ones are reordered.
        // Known issue: with reordering, water can come out white. Experimental, off by default.
        if (b > startBounce && !rayInWater(rayState) && !rayIgnoreWaterSelf(rayState)) {
            VPT_TRACE_PATH_REORDERED(VPT_RAY_FLAGS, mask, 1, 1, missId, mainRay.origin, 0.001, mainRay.direction, len);
        } else {
            VPT_TRACE_PATH(VPT_RAY_FLAGS, mask, 1, 1, missId, mainRay.origin, 0.001, mainRay.direction, len);
        }
        rayState = mainRay;
        raySetIgnoreWaterSelf(rayState, false);

        if (b == startBounce) {
            secondHitDistance = rayState.hitT;
            hasSecondHit = rayState.hitT != INF_DISTANCE;
#if VPT_RESTIR_GI != 0
            g_restirGiSecondNormal = hasSecondHit ? rayState.normal : vec3(0.0);
#endif
        }

        if (rayState.hitT != INF_DISTANCE) {
            MaterialInfo hitMat = rayLoadMaterial(rayState);
            vec3 hitWorldPos = tracedOrigin + tracedDirection * rayState.hitT;
#if SHARC_QUERY && USE_SHARC
            vec3 positionWorld = hitWorldPos + vec3(worldUBO.cameraPos.xyz);
            // With one bounce (VPT_BEDROCK_ONE_BOUNCE) a diffuse bounce off a surface the camera sees already lands
            // where the camera does not look: the cache answers there, as Bedrock's irradiance cache does.
            bool cacheFirstBounce = VPT_BEDROCK_ONE_BOUNCE != 0 && fromSeenSurface && leavingLobeType == 0u;
            // Not where the ray only passed through (glass crossed by light on its way): nothing is cached there,
            // and what another surface in the same cell holds is not what lies behind the glass.
            if (!rayIsPassThrough(rayState) &&
                sharcAllowQuery(isPrimaryCameraPath && !cacheFirstBounce,
                                (VPT_CACHE_DEEP_BOUNCES != 0 && b > startBounce) || cacheFirstBounce, rayState.hitT,
                                hitMat.roughness, rayLobeType(rayState), sharcParameters, positionWorld)) {
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
            sharcDebugBounceCount = max(sharcDebugBounceCount, b + 1);
#endif
            // Seen straight through (the side of a waterfall the view crosses, see default.rchit): not a bounce.
            bool passedThrough = rayIsPassThrough(rayState);
            bool isPerfectMirrorOrTransparent =
                passedThrough || ((hitMat.roughness < 0.02) && (rayLobeType(rayState) != 0u));
            if (!isPerfectMirrorOrTransparent) { isPrimaryCameraPath = false; }
            bool mirrorBounce = false;
            if ((passedThrough || (hitMat.roughness < 0.08 && rayLobeType(rayState) != 0u)) &&
                extraBounces < MIRROR_EXTRA_BOUNCES) {
                extraBounces++;
                mirrorBounce = true;
            }
            // One bounce is the budget of each surface the camera sees, not of the path: a surface seen through
            // water or glass, or in a reflection, gets its own bounce as the first hit does. Ended here, the floor
            // of every pool and sea and everything reflected in them had their sunlight and nothing else - no sky,
            // no light off the walls - and water read far darker than Bedrock RTX's.
            // (Only where it scatters next: the chain then ends, so a hall of mirrors stays bounded.)
            if (VPT_BEDROCK_ONE_BOUNCE != 0 && clearView && !mirrorBounce && rayLobeType(rayState) == 0u) {
                extraBounces++;
            }
        }

        // A chain of mirrors seen from the camera is not cut short at random: the reflection inside a reflection
        // would come out speckled. Its length is bounded by the mirror bounces allowed.
        if (b > 2 && !isPrimaryCameraPath) {
            float maxComponent = max(rayState.throughput.r, max(rayState.throughput.g, rayState.throughput.b));
            float rrProbability = min(maxComponent, 0.9);
            if (rand(rayState.seed) > rrProbability) { break; }
            rayState.throughput /= rrProbability;
        }
    }

    mainRay = rayState;
}

void traceTransparent(inout MainRay transparentRay,
                      int startBounce,
                      int isHand,
#if USE_SHARC && SHARC_QUERY
                      SharcParameters sharcParameters,
                      inout int sharcDebugBounceCount,
                      inout bool sharcDebugCacheHit,
#endif
                      out bool hasTransparentSecondHit,
                      out float transparentSecondHitDistance) {
    traceSecondary(transparentRay, startBounce, isHand,
#if USE_SHARC && SHARC_QUERY
                  sharcParameters, sharcDebugBounceCount, sharcDebugCacheHit,
#endif
                  hasTransparentSecondHit, transparentSecondHitDistance);
}

#ifndef VPT_HAND_SAMPLES
#    define VPT_HAND_SAMPLES 3
#endif

/**
 * traceSecondary, several times from the same first hit, averaged. For the held item and hand: they sit right in
 * front of the camera, big on screen, and a map or a block held up shows every speck of one-sample noise that the
 * upscaler has not yet smoothed over. The first hit and its direct light are shared; only the bounces after it are
 * taken again. The sample count is configurable because large held items can cover much of the screen.
 */
void traceSecondaryAveraged(int samples,
                            int startBounce,
                            int isHand,
#if USE_SHARC && SHARC_QUERY
                            SharcParameters sharcParameters,
                            inout int sharcDebugBounceCount,
                            inout bool sharcDebugCacheHit,
#endif
                            out bool hasSecondHit,
                            out float secondHitDistance) {
    MainRay start = mainRay;
    MainRay path = start;
    traceSecondary(path, startBounce, isHand,
#if USE_SHARC && SHARC_QUERY
                  sharcParameters, sharcDebugBounceCount, sharcDebugCacheHit,
#endif
                  hasSecondHit, secondHitDistance);
    vec3 radiance = path.radiance;
    for (int i = 1; i < samples; ++i) {
        uint seed = path.seed;
        path = start;
        path.seed = seed;
        bool extraHit;
        float extraDistance;
        traceSecondary(path, startBounce, isHand,
#if USE_SHARC && SHARC_QUERY
                      sharcParameters, sharcDebugBounceCount, sharcDebugCacheHit,
#endif
                      extraHit, extraDistance);
        radiance += path.radiance;
    }
    path.radiance = radiance / float(max(samples, 1));
    mainRay = path;
}

#endif // VPT_PT_BOUNCE_GLSL
