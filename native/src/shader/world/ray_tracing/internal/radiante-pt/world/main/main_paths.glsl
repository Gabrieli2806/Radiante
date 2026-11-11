// Part of main() in world/world.rgen: included there, in the middle of the function, and nowhere else. It is not a
// file of functions and has no include guard: it runs inside main() and shares its local variables with the parts
// before and after it.
//
// The light of the surface seen. The sky where the camera's ray ended in it; else the surface's own glow and direct
// light, then its bounces: a reflection and a refraction apart for water and clear glass, one path for everything
// else, with ReSTIR GI on the first diffuse bounce when it is on.

    bool hitSky = selectedFirstHit.hitSky;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // Stars are drawn only where the view ends in the sky: they have no medium of their own to fade them.
    float postStarCloudTransmittance = hitSky ? clamp(mainRay.directLightRadiance.x, 0.0, 1.0) : 0.0;
#else
    float postStarCloudTransmittance = hitSky ? clamp(mainRay.directLightRadiance.x, 0.0, 1.0) : 1.0;
#endif
    float viewDepth = selectedFirstHit.viewDepth;
    int startBounce = selectedFirstHit.startBounce;
    firstHitIncomingDirection = selectedFirstHit.incomingDirection;
    firstHitOutgoingDirection = selectedFirstHit.outgoingDirection;
    firstHitNormal = selectedFirstHit.normal;
    firstHitWorldPos = selectedFirstHit.worldPos;
    surfaceDepth = selectedFirstHit.surfaceDepth;

    imageStore(firstHitDepthImage, pixel, vec4(selectedFirstHit.firstHitLinearDepth));

    dvec3 currCameraPos = worldUBO.cameraPos.xyz;
    dvec3 lastCameraPos = lastWorldUBO.cameraPos.xyz;
    vec3 cameraDelta = vec3(currCameraPos - lastCameraPos);
    mat4 prevRelativeMVP = lastWorldUBO.cameraProjMat * lastWorldUBO.cameraEffectedViewMat;

    if (hitSky) {
        mainRay.radiance += glassReflectionRadiance;
        imageStore(outputImage, pixel, vec4(storedRadiance(mainRay.radiance), 1.0));
        // The sky's albedo guide is its colour, and what the denoiser takes for the light on it is the picture
        // divided by that guide. Tone mapped, the guide made that light at least 1 however dark the sky was, and at
        // dusk, at night or in fog - where the light on a block is a fraction of that - it bled into the pixels
        // along every silhouette: a pale rim on the edges of blocks and of the hand. Dividing by the brightest
        // channel instead keeps the sky's light on the scale of what is next to it.
        float skyLevel = max(max(mainRay.radiance.r, max(mainRay.radiance.g, mainRay.radiance.b)), SKY_GUIDE_MIN_LEVEL);
        imageStore(diffuseAlbedoImage, pixel, vec4(clamp(mainRay.radiance / skyLevel, 0.0, 1.0), 0));
        imageStore(specularAlbedoImage, pixel, vec4(0));
        imageStore(normalRoughnessImage, pixel, vec4(0.0));

        imageStore(linearDepthImage, pixel, vec4(INF_DISTANCE));
        vec2 motionVec = computeCameraMotionVector(prevRelativeMVP, pixelCenter, vec4(orgDirection, 0.0));
        imageStore(motionVectorImage, pixel, vec4(motionVec, vec2(0.0)));

        psrHitSky = true;
    } else {
        psrHitSky = false;

        hasSecondHit = false;
        distFirstToSecond = 0.0;
        firstHitEmission = vec3(0.0);
        firstHitDirectRadiance = vec3(0.0);
        firstHitIndirectRadiance = vec3(0.0);
        transparentReflectionRadiance = vec3(0.0);
        transparentRefractionRadiance = vec3(0.0);
        hasTransparentReflection = false;
        hasTransparentRefraction = false;
        hasTransparentReflectionSecondHit = false;
        transparentReflectionSecondHitDistance = 0.0;
        useTransparentSplit = false;
#    if USE_SHARC && SHARC_QUERY
        sharcDebugBounceCount = 0;
        sharcDebugCacheHit = false;
#    endif

        firstHitMat = rayLoadMaterial(mainRay);
        firstHitWaterSurface = rayLoadAux(mainRay).y > 0.5;
        imageStore(diffuseAlbedoImage, pixel,
                   vec4(firstHitMat.albedoValue.rgb * firstHitMat.albedoValue.a, firstHitMat.metallic));
        vec3 specularGuide = firstHitMat.f0;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        // The denoiser's specular guide as the reflectance in the view's direction, not at normal incidence.
        float guideNoV = abs(dot(-firstHitIncomingDirection, firstHitNormal));
        if (firstHitWaterSurface) {
            // The same interface weight as initTransparent, total internal reflection included. Aux holds the
            // medium the ray came in through, from before the hit shader swapped it.
            float guideEta = rayLoadAux(mainRay).y > 1.5 ? firstHitMat.ior : 1.0 / firstHitMat.ior;
            specularGuide = vec3(vptInterfaceFresnel(guideNoV, guideEta));
        } else if (firstHitMat.transmission <= 1e-6) {
            // Opaque glass borders and other shaded surfaces keep their own normals and roughness; the guide
            // follows their directional reflectance.
            specularGuide = reflectionAlbedoGuide(firstHitMat.f0, firstHitMat.roughness, guideNoV);
        }
#endif
        imageStore(specularAlbedoImage, pixel, vec4(specularGuide, 0.0));

        imageStore(normalRoughnessImage, pixel, vec4(mainRay.normal, firstHitMat.roughness));

        imageStore(linearDepthImage, pixel, vec4(viewDepth));

        {
            vec3 originRelativeToPrevCam = origin + orgDirection * selectedFirstHit.tracedDistance + cameraDelta;
            if (mainRay.hasPrevScenePos > 0u) { originRelativeToPrevCam = mainRay.prevScenePos; }
            // The held item and arm are fixed to the camera, not the world: they turn and move with it. Their world
            // position would send the upscaler's history looking for them where the world was, and they smeared
            // into ghosts whenever the camera turned. They are not fixed to the screen either, though: view bobbing
            // sways the view while they stay put, so a map in hand drifts across the screen with every step, and a
            // zero motion vector tore it apart while walking. Where the hand sat last frame is the same point in the
            // camera's own (unbobbed) frame; last frame's bobbed projection then says where that was on screen.
            // The held item also moves by itself: a map held in both hands rises and sinks as the view tilts, and
            // arms swing. Last frame's vertices of the hand say where it really was, but only roughly - a little
            // off from frame to frame even when the hand is still. Following them on every small difference, or
            // dropping the history on large ones, left whatever was held grainy: the upscaler kept starting over.
            // So the hand is taken as fixed to the camera unless it clearly moved against it.
            // The hand is traced with a field of view of its own (handFovScale): where a point of it shows on
            // screen is the world projection with x and y divided by that scale. Projecting it as world geometry
            // - and taking the point along the world ray rather than the hand ray - put its motion vectors off by
            // as much as the view moved, so everything held smeared and went grainy in motion.
            vec2 motionVec;
            if (isHand == 1) {
                vec3 handPos = origin + handDirection * selectedFirstHit.tracedDistance;
                vec3 handInView = mat3(worldUBO.cameraViewMat) * handPos;
                vec3 fixedInCamera = transpose(mat3(lastWorldUBO.cameraViewMat)) * handInView;
                motionVec = handMotionVector(prevRelativeMVP, pixelCenter, fixedInCamera);
                if (mainRay.hasPrevScenePos > 0u) {
                    vec2 realMotion = handMotionVector(prevRelativeMVP, pixelCenter, mainRay.prevScenePos);
                    // Moved against the camera by a few pixels or more (a swing, a map tilting with the pitch):
                    // follow it. A jump larger than any hand makes in a frame is a mismatch between last frame's
                    // vertices and this frame's (another item, another pose), not motion; the camera-fixed guess
                    // stands then.
                    float moved = length(realMotion - motionVec);
                    float screen = float(max(gl_LaunchSizeEXT.x, gl_LaunchSizeEXT.y));
                    if (moved > max(3.0, screen * 0.004) && moved < screen * 0.15) {
                        motionVec = realMotion;
                    }
                }
            } else {
                motionVec = computeCameraMotionVector(prevRelativeMVP, pixelCenter, vec4(originRelativeToPrevCam, 1.0));
            }
            imageStore(motionVectorImage, pixel, vec4(motionVec, 0.0, 0.0));
        }

        // (Bedrock RTX adds no light of its own: its constant ambient is zero, calibrated to Bedrock RTX.)
#if !defined(VPT_BEDROCK_ORIGINAL) || VPT_BEDROCK_ORIGINAL == 0
        if (startBounce == 1) {
            mainRay.radiance += VPT_BASIC_RADIANCE * firstHitMat.albedoValue.rgb * firstHitMat.albedoValue.a * mainRay.throughput;
        }
#endif

        psrHitPosition = firstHitWorldPos;

        isFirstHitNoisy = rayIsNoisy(mainRay);
        isFirstHitDiffuse = false;
        isFirstHitRefraction = false;
        isFirstHitTransparent = false;
        firstHitDirectRadiance = vec3(0.0);
        firstHitIndirectRadiance = vec3(0.0);
        if (isFirstHitNoisy) {
            // Same strength as the hit shaders give a glowing surface; see default.rchit.
#if VPT_BEDROCK_SPECULAR
            // Its colour a tenth of the way to grey, as Bedrock RTX shows a glow (block_light.glsl).
            vec3 glowColour = mix(firstHitMat.albedoValue.rgb,
                                  vec3(dot(firstHitMat.albedoValue.rgb, vec3(0.2126, 0.7152, 0.0722))), 0.1);
            firstHitEmission = glowColour * firstHitMat.emission * worldUBO.emissionBrightness *
                               VPT_BEDROCK_SEEN_EMISSION;
#else
            firstHitEmission = firstHitMat.albedoValue.rgb * firstHitMat.emission *
                               worldUBO.emissionBrightness * 16.0 * VPT_DIRECT_LIGHT_STRENGTH;
#endif
#ifndef RADIANTE_HOOK_BLOCK_LIGHT_DEFAULT
            // The same glow the closest hit added, through a pack's hooks/block_light.glsl.
            if (firstHitMat.emission > 0.0) { hookBlockLight(firstHitEmission, RADIANTE_BLOCK_LIGHT_GLOW); }
#endif
#if VPT_NRD_SIGNALS
            nrdFirstHitDirect = mainRay.directLightRadiance;
#endif
            if (rayLobeType(mainRay) == 0u) {
                isFirstHitDiffuse = true;
                firstHitDirectRadiance = mainRay.directLightRadiance;
            } else if (rayLobeType(mainRay) == 2u) {
                isFirstHitTransparent = true;
                float incomingSign = dot(firstHitIncomingDirection, firstHitNormal);
                float outgoingSign = dot(firstHitOutgoingDirection, firstHitNormal);
                isFirstHitRefraction = incomingSign * outgoingSign > 0.0;
            }
        }

        if (isFirstHitNoisy) {
            if (isFirstHitTransparent) {
                // Only for glass. A translucent solid (ice) is mostly shaded as a surface and only partly let
                // through; recomposing the frames that happened to go through as pure glass made it flicker.
                // Water too: its surface is traced as a reflection and a refraction each frame, so what lies under
                // shallow water stays sharp instead of the denoiser blending a random pick of the two. (Sheets of
                // water seen side on are passed straight through before this, see default.rchit.)
                useTransparentSplit = VPT_TRANSPARENT_SPLIT_MODE == VPT_TRANSPARENT_SPLIT_MODE_DETERMINISTIC &&
                                      firstHitMat.transmission > 0.95;

                if (useTransparentSplit) {
                    // Checkerboard (VPT_TRANSPARENT_CHECKERBOARD): one cell traces the reflection, the other the
                    // refraction, swapping every frame, each at twice its weight - one ray per pixel instead of two,
                    // as Bedrock RTX does for water and glass. The branch left out counts as traced and black.
                    bool traceReflection = true;
                    bool traceRefraction = true;
                    float branchWeight = 1.0;
                    if (VPT_TRANSPARENT_CHECKERBOARD != 0 && isHand == 0) {
                        traceReflection = checkerboardCell(pixel);
                        traceRefraction = !traceReflection;
                        branchWeight = 2.0;
                        hasTransparentReflection = !traceReflection;
                        hasTransparentRefraction = !traceRefraction;
                    }
                    // Both branches start from the ray as it left the first hit. Tracing the reflection leaves the
                    // end of its path in mainRay; started from that, the refraction no longer knew its surface was
                    // water (the mark is in the ray): it went straight on as through glass, tinted like glass, and
                    // not counted as in the water. Water seen from above had no depth to its colour, and where
                    // the view then left a thin body of water through its side - the top of a waterfall - that
                    // side counted as going into water, and all the world behind it turned deep-sea dark.
                    MainRay firstHitRay = mainRay;
                    MainRay transparentReflectionRay;
                    if (traceReflection &&
#ifdef RADIANTE_HOOK_RAY_ORIGINS_DEFAULT
                        initTransparent(firstHitRay, firstHitMat, firstHitIncomingDirection, firstHitNormal,
                                        firstHitWorldPos, 0x9E3779B9u, true, transparentReflectionRay)) {
#else
                        // From where the pack starts the surface's reflections, seen from there; the refraction
                        // below keeps the camera ray's point (hooks/ray_origins.glsl).
                        initTransparent(firstHitRay, firstHitMat, selectedFirstHit.reflectionIncoming,
                                        firstHitNormal, selectedFirstHit.reflectionPos, 0x9E3779B9u, true,
                                        transparentReflectionRay)) {
#endif
                        transparentReflectionRay.throughput *= branchWeight;
                        traceTransparent(transparentReflectionRay, startBounce, isHand,
#    if SHARC_QUERY && USE_SHARC
                                         sharcParameters, sharcDebugBounceCount, sharcDebugCacheHit,
#    endif
                                         hasTransparentReflectionSecondHit, transparentReflectionSecondHitDistance);
                        transparentReflectionRadiance = transparentReflectionRay.radiance;
                        hasTransparentReflection = true;
                    }
                    // The two branches replace the ray's own radiance; what the glass in front added goes with them.
                    transparentReflectionRadiance += g_passThroughRadiance;

                    MainRay transparentRefractionRay;
                    if (traceRefraction &&
                        initTransparent(firstHitRay, firstHitMat, firstHitIncomingDirection, firstHitNormal,
                                        firstHitWorldPos, 0x3C6EF372u, false, transparentRefractionRay)) {
                        transparentRefractionRay.throughput *= branchWeight;
                        bool hasTransparentRefractionSecondHit = false;
                        float transparentRefractionSecondHitDistance = 0.0;
                        traceTransparent(transparentRefractionRay, startBounce, isHand,
#    if SHARC_QUERY && USE_SHARC
                                         sharcParameters, sharcDebugBounceCount, sharcDebugCacheHit,
#    endif
                                         hasTransparentRefractionSecondHit, transparentRefractionSecondHitDistance);
                        transparentRefractionRadiance = transparentRefractionRay.radiance;
                        hasTransparentRefraction = true;
                    }

                    mainRay.radiance = transparentReflectionRadiance + transparentRefractionRadiance;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
                    // The water texture's own layer over the surface, lit where it was met (pt_shade_default.glsl).
                    if (firstHitWaterSurface) {
                        transparentRefractionRadiance += firstHitRay.directLightRadiance;
                        mainRay.radiance += firstHitRay.directLightRadiance;
                    }
#endif
                    mainRay.directLightRadiance = vec3(0.0);
                } else {
                    traceSecondary(mainRay, startBounce, isHand,
#    if SHARC_QUERY && USE_SHARC
                                  sharcParameters, sharcDebugBounceCount, sharcDebugCacheHit,
#    endif
                                  hasSecondHit, distFirstToSecond);
                }
            } else {
#if VPT_RESTIR_GI != 0
                vec3 restirGiOrigin = mainRay.origin;
                vec3 restirGiDirection = mainRay.direction;
                vec3 restirGiThroughput = mainRay.throughput;
                vec3 restirGiBefore = mainRay.radiance;
#endif
                traceSecondaryAveraged(isHand == 1 ? VPT_HAND_SAMPLES : 1, startBounce, isHand,
#    if SHARC_QUERY && USE_SHARC
                              sharcParameters, sharcDebugBounceCount, sharcDebugCacheHit,
#    endif
                              hasSecondHit, distFirstToSecond);
#if VPT_RESTIR_GI != 0
                // The first diffuse bounce of what the camera sees: resampled across frames and neighbours
                // (pt/pt_restir_gi.glsl). Not the hand: it moves with the camera, and averages its own samples.
                if (isFirstHitDiffuse && isHand == 0) {
                    float restirGiDistance = distFirstToSecond;
                    vec3 restirGiBounce = restirGiResolve(
                        pixel, ivec2(gl_LaunchSizeEXT.xy), firstHitWorldPos, normalize(firstHitNormal), restirGiOrigin,
                        restirGiDirection, restirGiThroughput, mainRay.radiance - restirGiBefore, hasSecondHit,
                        restirGiDistance, cameraDelta, prevRelativeMVP, mainRay.seed);
                    mainRay.radiance = restirGiBefore + restirGiBounce;
                    hasSecondHit = restirGiDistance > 0.0;
                    distFirstToSecond = restirGiDistance;
                }
#endif

                if (isFirstHitDiffuse) { firstHitIndirectRadiance = mainRay.radiance - firstHitDirectRadiance; }
            }
        } else {
            traceSecondaryAveraged(isHand == 1 ? VPT_HAND_SAMPLES : 1, startBounce, isHand,
#    if SHARC_QUERY && USE_SHARC
                          sharcParameters, sharcDebugBounceCount, sharcDebugCacheHit,
#    endif
                          hasSecondHit, distFirstToSecond);
        }
    }
