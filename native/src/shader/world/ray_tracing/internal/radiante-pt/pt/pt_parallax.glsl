#ifndef VPT_PT_PARALLAX_GLSL
#define VPT_PT_PARALLAX_GLSL

// Height-map parallax: the traces through a carved texture (primary, shadow and bounce), and at the primary hit
// the first carved surface the view meets plus the outer-edge probe that lets the view go past a carved-away
// corner. Moved out of the default closest-hit shader unchanged; the hit is read through g_hit.

vec3 basePlaneWorldPosAtUv(vec2 uv,
                           vec2 referenceUv,
                           vec3 referenceWorldPos,
                           vec3 dPduWorld,
                           vec3 dPdvWorld) {
    vec2 uvOffset = uv - referenceUv;
    return referenceWorldPos + dPduWorld * uvOffset.x + dPdvWorld * uvOffset.y;
}

void initVanillaParallaxMiss(vec2 uv, float depth, vec3 baseNormal, out HeightMapHit hit) {
    hit.hit = false;
    hit.sideWall = false;
    hit.edgeWall = false;
    hit.t = INF_DISTANCE;
    hit.uv = uv;
    hit.depth = depth;
    hit.geometricNormal = baseNormal;
}

bool traceNearestHeightMapCapped(sampler2D tex,
                                 vec2 minUV,
                                 vec2 maxUV,
                                 vec2 uv,
                                 float depth,
                                 vec3 worldDir,
                                 vec3 dPdu,
                                 vec3 dPdv,
                                 vec3 baseNormal,
                                 float maxDepth,
                                 int maxTraceSteps,
                                 out HeightMapHit hit) {
    initVanillaParallaxMiss(uv, depth, baseNormal, hit);
    if (maxTraceSteps <= 0 || maxDepth <= heightMapMinWorldDepth) { return false; }

    ivec2 size = textureSize(tex, 0);
    if (size.x <= 0 || size.y <= 0) { return false; }

    vec2 boundsMin = heightMapMinUV(minUV, maxUV, size);
    vec2 boundsMax = heightMapMaxUV(minUV, maxUV, size);
    if (!isUvWithinHeightMap(uv, boundsMin, boundsMax)) { return false; }
    float minEdgeDepth = edgeMinDepth(minUV, maxUV, size, maxDepth);

    vec2 rateUV = directionToRateUv(worldDir, dPdu, dPdv);
    float depthRate = dot(worldDir, -baseNormal);

    // A tile's edges fall on whole texels, but atlas coordinates carry float error: ceil() of a max edge that came
    // out a hair above its texel boundary took in the first texel of the next sprite, whose heights then stood
    // as a one texel wall - a black line along every block seam, seen only when looking towards that edge.
    ivec2 atlasTexelMin = clampTexelCoord(ivec2(round(min(minUV, maxUV) * vec2(size))), size);
    ivec2 atlasTexelMax = clampTexelCoord(ivec2(round(max(minUV, maxUV) * vec2(size))) - ivec2(1), size);
    ivec2 texel = clampTexelCoord(ivec2(floor(uv * vec2(size))), size);
    if (any(lessThan(texel, atlasTexelMin)) || any(greaterThan(texel, atlasTexelMax))) { return false; }

    int remainingUSteps = heightMapRemainingSteps(texel.x, atlasTexelMin.x, atlasTexelMax.x, rateUV.x);
    int remainingVSteps = heightMapRemainingSteps(texel.y, atlasTexelMin.y, atlasTexelMax.y, rateUV.y);
    int maxSteps = min(maxTraceSteps, min(heightMapNearestMaxSteps, remainingUSteps + remainingVSteps + 4));
    if (maxSteps <= 0) { return false; }

    float tCurrent = 0.0;
    vec2 uvCurrent = uv;
    float depthCurrent = depth;

    int step = 0;
    while (step < maxSteps) {
        if (depthCurrent < -heightMapTraceBias) { break; }
        if (any(lessThan(texel, atlasTexelMin)) || any(greaterThan(texel, atlasTexelMax))) { break; }

        float surfaceDepth = sampleHeightDepthNearest(tex, texel, atlasTexelMin, atlasTexelMax, uvCurrent, maxDepth);
        if (depthCurrent >= surfaceDepth - heightMapTraceBias) {
            hit.hit = true;
            hit.sideWall = false;
            hit.edgeWall = false;
            hit.t = tCurrent;
            hit.uv = uvCurrent;
            hit.depth = max(depthCurrent, surfaceDepth);
            hit.geometricNormal = baseNormal;
            return true;
        }

        float tTop = INF_DISTANCE;
        if (depthRate > 1e-6 && surfaceDepth > depthCurrent) { tTop = (surfaceDepth - depthCurrent) / depthRate; }

        float tU = INF_DISTANCE;
        float tV = INF_DISTANCE;
        bool stepU = false;
        bool stepV = false;

        if (rateUV.x > 1e-9) {
            float boundary = (float(texel.x + 1)) / float(size.x);
            tU = (boundary - uvCurrent.x) / rateUV.x;
            stepU = true;
        } else if (rateUV.x < -1e-9) {
            float boundary = float(texel.x) / float(size.x);
            tU = (boundary - uvCurrent.x) / rateUV.x;
            stepU = true;
        }

        if (rateUV.y > 1e-9) {
            float boundary = (float(texel.y + 1)) / float(size.y);
            tV = (boundary - uvCurrent.y) / rateUV.y;
            stepV = true;
        } else if (rateUV.y < -1e-9) {
            float boundary = float(texel.y) / float(size.y);
            tV = (boundary - uvCurrent.y) / rateUV.y;
            stepV = true;
        }

        float tAbove = INF_DISTANCE;
        if (depthRate < -1e-6) { tAbove = -depthCurrent / depthRate; }

        float tEdge = min(tU, tV);
        if (tTop <= min(tEdge, tAbove) + heightMapTraceBias && tTop < INF_DISTANCE) {
            tCurrent += max(tTop, 0.0);
            hit.hit = true;
            hit.sideWall = false;
            hit.edgeWall = false;
            hit.t = tCurrent;
            hit.uv = uvCurrent + rateUV * max(tTop, 0.0);
            hit.depth = surfaceDepth;
            hit.geometricNormal = baseNormal;
            return true;
        }

        if (tAbove <= tEdge + heightMapTraceBias) { break; }
        if (tEdge == INF_DISTANCE) { break; }

        float dt = max(tEdge, 0.0);
        tCurrent += dt;

        vec2 edgeUV = uvCurrent + rateUV * dt;
        float edgeDepth = depthCurrent + depthRate * dt;
        bool cornerStep = stepU && stepV && abs(tU - tV) <= heightMapTraceBias;
        if (cornerStep) {
            ivec2 texelU = texel + ivec2(rateUV.x > 0.0 ? 1 : -1, 0);
            ivec2 texelV = texel + ivec2(0, rateUV.y > 0.0 ? 1 : -1);
            ivec2 texelD = texel + ivec2(rateUV.x > 0.0 ? 1 : -1, rateUV.y > 0.0 ? 1 : -1);
            bool inU = all(greaterThanEqual(texelU, atlasTexelMin)) && all(lessThanEqual(texelU, atlasTexelMax));
            bool inV = all(greaterThanEqual(texelV, atlasTexelMin)) && all(lessThanEqual(texelV, atlasTexelMax));
            bool inD = all(greaterThanEqual(texelD, atlasTexelMin)) && all(lessThanEqual(texelD, atlasTexelMax));

            if (inU && inV && inD) {
                float depthUCorner =
                    sampleHeightDepthNearest(tex, texelU, atlasTexelMin, atlasTexelMax, edgeUV, maxDepth);
                float depthVCorner =
                    sampleHeightDepthNearest(tex, texelV, atlasTexelMin, atlasTexelMax, edgeUV, maxDepth);
                float depthDCorner =
                    sampleHeightDepthNearest(tex, texelD, atlasTexelMin, atlasTexelMax, edgeUV, maxDepth);
                bool enteredWallU = enteredTexelWall(surfaceDepth, depthUCorner, edgeDepth) ||
                                    enteredTexelWall(depthVCorner, depthDCorner, edgeDepth);
                bool enteredWallV = enteredTexelWall(surfaceDepth, depthVCorner, edgeDepth) ||
                                    enteredTexelWall(depthUCorner, depthDCorner, edgeDepth);

                if (enteredWallU || enteredWallV) {
                    hit.hit = true;
                    hit.sideWall = true;
                    hit.edgeWall = false;
                    hit.t = tCurrent;
                    hit.uv = edgeUV;
                    hit.depth = edgeDepth;
                    hit.geometricNormal = wallNormal(enteredWallU, enteredWallV, rateUV, dPdu, dPdv, baseNormal);
                    return true;
                }

                texel = texelD;
                uvCurrent = edgeUV;
                depthCurrent = edgeDepth;
                ++step;
                continue;
            }
        }

        bool processU = false;
        if (tU <= tV + heightMapTraceBias) {
            processU = tU < tV - heightMapTraceBias ? true : abs(rateUV.x) >= abs(rateUV.y);
        }

        ivec2 nextTexel = texel;
        bool enteredSideWall = false;
        vec3 enteredWallNormal = baseNormal;
        bool enteredTop = false;

        if (processU) {
            nextTexel += ivec2(rateUV.x > 0.0 ? 1 : -1, 0);
            if (any(lessThan(nextTexel, atlasTexelMin)) || any(greaterThan(nextTexel, atlasTexelMax))) {
                vec3 edgeNormal;
                if (isEdge(edgeDepth, surfaceDepth, minEdgeDepth, edgeUV, boundsMin, boundsMax, rateUV, dPdu, dPdv,
                           baseNormal, edgeNormal)) {
                    hit.hit = true;
                    hit.sideWall = true;
                    hit.edgeWall = true;
                    hit.t = tCurrent;
                    hit.uv = clamp(edgeUV, boundsMin, boundsMax);
                    hit.depth = edgeDepth;
                    hit.geometricNormal = edgeNormal;
                    return true;
                }
                break;
            }

            float nextDepth =
                sampleHeightDepthNearest(tex, nextTexel, atlasTexelMin, atlasTexelMax, edgeUV, maxDepth);
            if (enteredTexelWall(surfaceDepth, nextDepth, edgeDepth)) {
                enteredSideWall = true;
                enteredWallNormal = wallNormal(true, false, rateUV, dPdu, dPdv, baseNormal);
            } else if (shouldContinueTexelTop(surfaceDepth, nextDepth, edgeDepth)) {
                enteredTop = true;
            }
        }

        if (!processU) {
            nextTexel = texel + ivec2(0, rateUV.y > 0.0 ? 1 : -1);
            if (any(lessThan(nextTexel, atlasTexelMin)) || any(greaterThan(nextTexel, atlasTexelMax))) {
                vec3 edgeNormal;
                if (isEdge(edgeDepth, surfaceDepth, minEdgeDepth, edgeUV, boundsMin, boundsMax, rateUV, dPdu, dPdv,
                           baseNormal, edgeNormal)) {
                    hit.hit = true;
                    hit.sideWall = true;
                    hit.edgeWall = true;
                    hit.t = tCurrent;
                    hit.uv = clamp(edgeUV, boundsMin, boundsMax);
                    hit.depth = edgeDepth;
                    hit.geometricNormal = edgeNormal;
                    return true;
                }
                break;
            }

            float nextDepth =
                sampleHeightDepthNearest(tex, nextTexel, atlasTexelMin, atlasTexelMax, edgeUV, maxDepth);
            if (enteredTexelWall(surfaceDepth, nextDepth, edgeDepth)) {
                enteredSideWall = true;
                enteredWallNormal = wallNormal(false, true, rateUV, dPdu, dPdv, baseNormal);
            } else if (shouldContinueTexelTop(surfaceDepth, nextDepth, edgeDepth)) {
                enteredTop = true;
            }
        }

        if (enteredSideWall) {
            hit.hit = true;
            hit.sideWall = true;
            hit.edgeWall = false;
            hit.t = tCurrent;
            hit.uv = edgeUV;
            hit.depth = edgeDepth;
            hit.geometricNormal = enteredWallNormal;
            return true;
        }

        if (enteredTop) {
            hit.hit = true;
            hit.sideWall = false;
            hit.edgeWall = false;
            hit.t = tCurrent;
            hit.uv = edgeUV;
            hit.depth = surfaceDepth;
            hit.geometricNormal = baseNormal;
            return true;
        }

        texel = nextTexel;
        uvCurrent = edgeUV;
        depthCurrent = edgeDepth;
        ++step;
    }

    return false;
}

bool traceBilinearHeightMapCapped(sampler2D tex,
                                  vec2 minUV,
                                  vec2 maxUV,
                                  vec2 uv,
                                  float depth,
                                  vec3 worldDir,
                                  vec3 dPdu,
                                  vec3 dPdv,
                                  vec3 baseNormal,
                                  float maxDepth,
                                  int maxTraceSteps,
                                  out HeightMapHit hit) {
    initVanillaParallaxMiss(uv, depth, baseNormal, hit);
    if (maxTraceSteps <= 0 || maxDepth <= heightMapMinWorldDepth) { return false; }

    ivec2 size = textureSize(tex, 0);
    if (size.x <= 0 || size.y <= 0) { return false; }

    vec2 boundsMin = heightMapMinUV(minUV, maxUV, size);
    vec2 boundsMax = heightMapMaxUV(minUV, maxUV, size);
    if (!isUvWithinHeightMap(uv, boundsMin, boundsMax)) { return false; }
    float minEdgeDepth = edgeMinDepth(minUV, maxUV, size, maxDepth);

    vec2 rateUV = directionToRateUv(worldDir, dPdu, dPdv);
    float depthRate = dot(worldDir, -baseNormal);
    float texelTravel = max(abs(rateUV.x) * float(size.x), abs(rateUV.y) * float(size.y));
    float depthTravel = abs(depthRate) / max(maxDepth, heightMapMinWorldDepth) * float(max(size.x, size.y));
    float stepWorld = 0.5 / max(max(texelTravel, depthTravel), 1e-4);

    float tPrev = 0.0;
    vec2 uvPrev = uv;
    float depthPrev = depth;
    float surfacePrev = sampleHeightDepth(tex, uvPrev, minUV, maxUV, 0, 1u, maxDepth);
    float fPrev = depthPrev - surfacePrev;
    if (fPrev >= -heightMapTraceBias) {
        hit.hit = true;
        hit.t = 0.0;
        hit.uv = uvPrev;
        hit.depth = surfacePrev;
        hit.geometricNormal =
            sampleNormal(tex, uvPrev, minUV, maxUV, dPdu, dPdv, baseNormal, 0, 1u, maxDepth, -worldDir);
        return true;
    }

    int maxSteps = min(maxTraceSteps, heightMapBilinearMaxSteps);
    for (int step = 0; step < maxSteps; ++step) {
        float tCurr = tPrev + stepWorld;
        vec2 uvCurr = uv + rateUV * tCurr;
        float depthCurr = depth + depthRate * tCurr;
        if (!isUvWithinHeightMap(uvCurr, boundsMin, boundsMax)) {
            float tEdge = heightMapBoundaryHitT(tPrev, tCurr, uvPrev, uvCurr, boundsMin, boundsMax);
            vec2 uvClamped = clamp(uv + rateUV * tEdge, boundsMin, boundsMax);
            float depthEdge = depth + depthRate * tEdge;
            vec3 edgeNormal;
            float boundaryDepth = sampleHeightDepth(tex, uvClamped, minUV, maxUV, 0, 1u, maxDepth);
            if (isEdge(depthEdge, boundaryDepth, minEdgeDepth, uvClamped, boundsMin, boundsMax, rateUV, dPdu, dPdv,
                       baseNormal, edgeNormal)) {
                hit.hit = true;
                hit.sideWall = true;
                hit.edgeWall = true;
                hit.t = tEdge;
                hit.uv = uvClamped;
                hit.depth = depthEdge;
                hit.geometricNormal = edgeNormal;
                return true;
            }
            break;
        }
        if (depthCurr < -heightMapTraceBias) { break; }

        float surfaceCurr = sampleHeightDepth(tex, uvCurr, minUV, maxUV, 0, 1u, maxDepth);
        float fCurr = depthCurr - surfaceCurr;
        if (fCurr >= -heightMapTraceBias) {
            float lo = tPrev;
            float hi = tCurr;
            for (int refine = 0; refine < heightMapBilinearBinarySteps; ++refine) {
                float mid = 0.5 * (lo + hi);
                vec2 uvMid = uv + rateUV * mid;
                float depthMid = depth + depthRate * mid;
                float surfaceMid = sampleHeightDepth(tex, uvMid, minUV, maxUV, 0, 1u, maxDepth);
                if (depthMid >= surfaceMid) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }

            float tHit = hi;
            vec2 uvHit = uv + rateUV * tHit;
            float depthHit = sampleHeightDepth(tex, uvHit, minUV, maxUV, 0, 1u, maxDepth);
            hit.hit = true;
            hit.sideWall = false;
            hit.edgeWall = false;
            hit.t = tHit;
            hit.uv = uvHit;
            hit.depth = depthHit;
            hit.geometricNormal =
                sampleNormal(tex, uvHit, minUV, maxUV, dPdu, dPdv, baseNormal, 0, 1u, maxDepth, -worldDir);
            return true;
        }

        tPrev = tCurr;
        uvPrev = uvCurr;
        depthPrev = depthCurr;
        fPrev = fCurr;
    }

    return false;
}

bool traceHeightMapCapped(sampler2D tex,
                          vec2 minUV,
                          vec2 maxUV,
                          vec2 uv,
                          float depth,
                          vec3 worldDir,
                          vec3 dPdu,
                          vec3 dPdv,
                          vec3 baseNormal,
                          float maxDepth,
                          uint samplingMode,
                          int maxTraceSteps,
                          out HeightMapHit hit) {
    if (samplingMode == 0u) {
        return traceNearestHeightMapCapped(tex, minUV, maxUV, uv, depth, worldDir, dPdu, dPdv, baseNormal, maxDepth,
                                           maxTraceSteps, hit);
    }

    return traceBilinearHeightMapCapped(tex, minUV, maxUV, uv, depth, worldDir, dPdu, dPdv, baseNormal, maxDepth,
                                        maxTraceSteps, hit);
}

bool traceLocalHeightIntersectionAndExit(int normalTextureID,
                                         vec2 atlasUvMin,
                                         vec2 atlasUvMax,
                                         vec3 dPduWorld,
                                         vec3 dPdvWorld,
                                         vec3 baseGeoNormal,
                                         float maxDepthWorld,
                                         SampledSurface surface,
                                         vec3 worldDir,
                                         int maxTraceSteps,
                                         out HeightMapHit hit,
                                         out vec2 exitUv,
                                         out float exitDepth,
                                         out float exitDistance) {
    ivec2 heightMapSize = textureSize(textures[nonuniformEXT(normalTextureID)], 0);
    if (normalTextureID < 0 || maxDepthWorld <= heightMapMinWorldDepth) { return false; }
    if (heightMapSize.x <= 0 || heightMapSize.y <= 0) { return false; }
    vec2 traceAtlasUvMin = heightMapInnerMinUV(atlasUvMin, atlasUvMax, heightMapSize);
    vec2 traceAtlasUvMax = heightMapInnerMaxUV(atlasUvMin, atlasUvMax, heightMapSize);
    vec2 traceSurfaceUv = surface.edgeWall ? clamp(surface.uv, traceAtlasUvMin, traceAtlasUvMax) : surface.uv;

    initVanillaParallaxMiss(traceSurfaceUv, surface.depth, baseGeoNormal, hit);
    exitUv = traceSurfaceUv;
    exitDepth = surface.depth;
    exitDistance = 0.0;

    float startBias = 2.0 * heightMapTraceBias;
    vec2 rateUV = directionToRateUv(worldDir, dPduWorld, dPdvWorld);
    float depthRate = dot(worldDir, -baseGeoNormal);
    vec2 uv = traceSurfaceUv + rateUV * startBias;
    float depth = surface.depth + depthRate * startBias;
    if (dot(surface.geometricNormal, baseGeoNormal) > 0.5 && depthRate < 0.0) {
        depth = min(depth, surface.depth - startBias);
    }

    bool fallbackToBilinear =
        VPT_PBR_SAMPLING_MODE == 0u && shouldFallbackNearestSecondaryToBilinear(worldDir, baseGeoNormal);
    bool localHit;
    if (depthRate < -1e-6) {
        float maxTraceDistance = max(depth / -depthRate, 0.0);
        if (fallbackToBilinear) {
            localHit =
                traceBilinearHeightMap(textures[nonuniformEXT(normalTextureID)], traceAtlasUvMin, traceAtlasUvMax, uv, depth,
                                       worldDir, dPduWorld, dPdvWorld, baseGeoNormal, maxDepthWorld,
                                       maxTraceDistance, hit);
        } else {
            localHit =
                traceHeightMap(textures[nonuniformEXT(normalTextureID)], traceAtlasUvMin, traceAtlasUvMax, uv, depth, worldDir,
                               dPduWorld, dPdvWorld, baseGeoNormal, maxDepthWorld, maxTraceDistance,
                               VPT_PBR_SAMPLING_MODE, hit);
        }
    } else if (fallbackToBilinear) {
        localHit = traceBilinearHeightMapCapped(textures[nonuniformEXT(normalTextureID)], traceAtlasUvMin, traceAtlasUvMax, uv,
                                                depth, worldDir, dPduWorld, dPdvWorld, baseGeoNormal, maxDepthWorld,
                                                maxTraceSteps, hit);
    } else {
        localHit = traceHeightMapCapped(textures[nonuniformEXT(normalTextureID)], traceAtlasUvMin, traceAtlasUvMax, uv, depth,
                                        worldDir, dPduWorld, dPdvWorld, baseGeoNormal, maxDepthWorld,
                                        VPT_PBR_SAMPLING_MODE, maxTraceSteps, hit);
    }

    if (localHit) { return true; }

    if (depthRate >= -1e-6) { return true; }

    float exitT = max(depth / -depthRate, 0.0);
    exitUv = clamp(uv + rateUV * exitT, traceAtlasUvMin, traceAtlasUvMax);
    exitDepth = 0.0;
    exitDistance = exitT;
    return false;
}

// The carved surface the view meets first, when the face is traced (traceLocalHeight); the flat face otherwise.
HeightMapHit ptParallaxInitialHit(bool traceLocalHeight,
                                  TextureMapEntry textureMap,
                                  vec2 textureUV,
                                  vec2 atlasUvMin,
                                  vec2 atlasUvMax,
                                  vec3 dPduWorld,
                                  vec3 dPdvWorld,
                                  vec3 baseGeoNormal,
                                  float maxDepthWorld) {
    HeightMapHit initialHit;
    initialHit.hit = false;
    initialHit.sideWall = false;
    initialHit.edgeWall = false;
    initialHit.t = 0.0;
    initialHit.uv = textureUV;
    initialHit.depth = 0.0;
    initialHit.geometricNormal = baseGeoNormal;
    if (traceLocalHeight) {
        HeightMapHit tracedInitialHit;
        gParallaxDetectEdges = worldUBO.parallaxTransparentEdges != 0u;
        if (traceHeightMapCapped(textures[nonuniformEXT(textureMap.normal)], atlasUvMin, atlasUvMax, textureUV, 0.0,
                                 g_hit.rayDir, dPduWorld, dPdvWorld, baseGeoNormal, maxDepthWorld,
                                 VPT_PBR_SAMPLING_MODE, VPT_PARALLAX_PRIMARY_MAX_STEPS, tracedInitialHit)) {
            initialHit = tracedInitialHit;
        }
        gParallaxDetectEdges = false;
    }
    return initialHit;
}

// Whether the view reached a carved-away outer edge and has been sent on past it (the hit is then a pass-through
// and the shading stops here). Otherwise an edge hit is turned back into the flat border.
bool ptParallaxEdgePassThrough(inout HeightMapHit initialHit, vec3 baseGeoNormal) {
    if (initialHit.edgeWall) {
        // The ray reached the edge of this face below its carved surface. At an outer edge, with air past it, that
        // corner is carved away too: the ray goes on and shows what is behind. Between two blocks the next face
        // carries on instead, so there the edge keeps the border's colour. The two are told apart by the block's
        // own face around the corner: Minecraft only keeps it where nothing covers it.
        vec3 edgePos = g_hit.planeHitWorldPos + g_hit.rayDir * initialHit.t;
        vec3 outward = -initialHit.geometricNormal;
        shadowRay.radiance = vec3(0.0);
        shadowRay.throughput = vec3(1.0);
        shadowRay.insideBoat = 0u;
        shadowRay.pad0 = BLOCK_LIGHT_QUERY;
        traceRayEXT(topLevelAS, gl_RayFlagsNoneEXT, WORLD_MASK, 0, 0, 0, edgePos - outward * 0.005, 0.0001, outward,
                    0.02, 1);
        shadowRay.pad0 = 0u;
        bool outerEdge = shadowRay.radiance.r < 0.5;
        if (outerEdge) {
            mainRay.hitT = g_hit.hitT + initialHit.t;
            mainRay.coneWidth += mainRay.hitT * mainRay.coneSpread;
            // Past the corner face along its normal, not just along the ray: a ray grazing that face would
            // otherwise start on its inner side and hit it from within.
            mainRay.origin = edgePos + outward * 0.003 + g_hit.rayDir * 0.001;
            ptClearReflectionRequest();
            mainRay.hasPrevScenePos = 0u;
            ptStoreMaterial(vec4(1.0), vec3(0.04), 1.0, 0.0, 1.0, 1.0, 0.0);
            raySetNoisy(mainRay, false);
            raySetContinue(mainRay, true);
            raySetPassThrough(mainRay, true);
            raySetStop(mainRay, false);
            return true;
        }
        initialHit.sideWall = false;
        initialHit.edgeWall = false;
        initialHit.geometricNormal = baseGeoNormal;
    }
    return false;
}

#endif // VPT_PT_PARALLAX_GLSL
