// Part of main() in world/world.rgen: included there, in the middle of the function, and nowhere else. It is not a
// file of functions and has no include guard: it runs inside main() and shares its local variables with the parts
// before and after it.
//
// The camera's rays. Declares what the rest of main() keeps about the pixel (the surface seen, its light, the fog to
// compose), traces the world's ray and, in first person, the hand's, takes the nearer of the two as the surface seen,
// and adds the world a pane of glass in front of it reflects.

    float surfaceDepth = INF_DISTANCE;
    bool psrHitSky = false;
    vec3 psrHitPosition;

    MaterialInfo firstHitMat;
    bool hasSecondHit = false;
    float distFirstToSecond = 0.0;
    bool isFirstHitNoisy = false;
    bool firstHitWaterSurface = false;
    bool isFirstHitDiffuse = false;
    bool isFirstHitRefraction = false;
    bool isFirstHitTransparent = false;
    vec3 firstHitEmission = vec3(0.0);
    vec3 firstHitDirectRadiance = vec3(0.0);
    vec3 firstHitIndirectRadiance = vec3(0.0);
    vec3 firstHitIncomingDirection = vec3(0.0);
    vec3 firstHitOutgoingDirection = vec3(0.0);
    vec3 firstHitNormal = vec3(0.0);
    vec3 firstHitWorldPos = vec3(0.0);
    vec3 transparentReflectionRadiance = vec3(0.0);
    vec3 transparentRefractionRadiance = vec3(0.0);
    bool hasTransparentReflection = false;
    bool hasTransparentRefraction = false;
    bool hasTransparentReflectionSecondHit = false;
    float transparentReflectionSecondHitDistance = 0.0;
    bool useTransparentSplit = false;
    vec3 composeFogAdditive = vec3(0.0);
    float composeFogTransmittance = -1.0;
    vec3 composeFogTransmissionRGB = vec3(1.0);
    bool composeFogIsRgb = false;
    // The medium the camera is in (cameraMedium), and how much of what the camera's ray hit shows through it, for the
    // denoiser's guides below; 1 outside any.
    vec3 cameraMediumSigmaT;
    vec3 cameraMediumLight;
    vec3 cameraMediumMeter;
    float cameraMediumExposure;
    bool cameraInMedium =
        cameraMedium(eyePos, cameraMediumSigmaT, cameraMediumLight, cameraMediumMeter, cameraMediumExposure);
    vec3 cameraMediumTransmittance = vec3(1.0);
#    if SHARC_QUERY && USE_SHARC
    SharcParameters sharcParameters = makeSharcParameters();
    int sharcDebugBounceCount = 0;
    bool sharcDebugCacheHit = false;
#    endif

    MainRay worldRay;
    initMainRay(origin, direction, worldUBO.seed, coneSpreadFromFov(fovY, fovX, resolution), worldRay);
    FirstHitPayload worldFirstHit;
    g_glassReflection = false;
#if VPT_NRD_SIGNALS
    clearNrdRecord(pixel);
#endif
    tracePrimary(eyePos, orgDirection, rayFlags, 0, worldRay, worldFirstHit);
#if VPT_NRD_SIGNALS
    vec3 worldNrdEmission = readNrdRecord(pixel);
    clearNrdRecord(pixel);
#endif
    bool worldGlassReflection = g_glassReflection;
    vec3 worldPassThroughTint = g_passThroughTint;
    vec3 worldPassThroughRadiance = g_passThroughRadiance;
    float worldPassThroughDistance = g_passThroughDistance;


    // Minecraft draws the hand with a fixed field of view of its own, so it keeps its size when the FOV setting
    // changes (GameRenderer.render3dHud builds its projection from cameraState.hudFov). The rays that look for the
    // hand carry the same frustum; everything else keeps the world's.
    vec3 handDirection = orgDirection;
    if (worldUBO.handFovScale > 0.0 && abs(worldUBO.handFovScale - 1.0) > 1e-4) {
        handDirection = normalize(vec3(worldUBO.cameraEffectedViewMatInv *
                                       vec4(viewNear.xy * worldUBO.handFovScale, viewNear.z, 0.0)));
    }

    MainRay handRay;
    initMainRay(origin, handDirection, worldUBO.seed ^ 0x9E3779B9u, coneSpreadFromFov(fovY, fovX, resolution),
                handRay);
    FirstHitPayload handFirstHit;
    // Only while the hands are drawn (bit 1 of isFirstPerson): with none to find, this second camera ray per pixel cost
    // a quarter of a millisecond at 1280x720 for nothing (spectator, F1, third person).
    // ndc already includes the exact temporal jitter used by handDirection. Two additional pixels cover
    // projection roundoff and edge samples at any internal resolution/aspect ratio. Animation needs no prediction:
    // the envelope encloses the very same fully posed triangles and transforms used by this frame's TLAS.
    vec2 handPadding = 4.0 / resolution;
    bool inHandBounds = all(greaterThanEqual(ndc, worldUBO.handPrimaryBounds.xy - handPadding)) &&
                        all(lessThanEqual(ndc, worldUBO.handPrimaryBounds.zw + handPadding));
    bool predictedHandBounds = inHandBounds;
#if VPT_HAND_PRIMARY_BOUNDS == 0 || VPT_HAND_PRIMARY_BOUNDS == 3
    inHandBounds = true;
#endif
    if ((worldUBO.isFirstPerson & 2u) != 0u && inHandBounds) {
        tracePrimary(eyePos, handDirection, rayFlags, 1, handRay, handFirstHit);
    } else {
        handFirstHit = worldFirstHit;
        handFirstHit.surfaceDepth = INF_DISTANCE;
    }
#if VPT_NRD_SIGNALS
    vec3 nrdEmission = handFirstHit.surfaceDepth != INF_DISTANCE ? readNrdRecord(pixel) : worldNrdEmission;
    // The direct light of the surface seen, all of it, before the bounces replace the ray's.
    vec3 nrdFirstHitDirect = vec3(0.0);
#endif
    vec3 handPassThroughTint = g_passThroughTint;
    vec3 handPassThroughRadiance = g_passThroughRadiance;
    float handPassThroughDistance = g_passThroughDistance;

    int isHand = 0;
    g_glassReflection = worldGlassReflection;
    FirstHitPayload selectedFirstHit = worldFirstHit;
    mainRay = worldRay;
    g_passThroughTint = worldPassThroughTint;
    g_passThroughRadiance = worldPassThroughRadiance;
    g_passThroughDistance = worldPassThroughDistance;
    if (handFirstHit.surfaceDepth != INF_DISTANCE) {
        isHand = 1;
        selectedFirstHit = handFirstHit;
        mainRay = handRay;
        g_passThroughTint = handPassThroughTint;
        g_passThroughRadiance = handPassThroughRadiance;
        g_passThroughDistance = handPassThroughDistance;
    }

    // The glass's reflection of the world, traced from where the camera's ray met it (see default.rchit). Added to
    // the picture and, as it is not part of the surface behind, to the channel the denoiser leaves as it is.
    vec3 glassReflectionRadiance = vec3(0.0);
    if (g_glassReflection && isHand == 0) {
        MainRay saved = mainRay;
        // The surfaces the reflection meets store their materials in this pixel's material image too, over the
        // surface the camera sees through the glass. Read back after it, the denoiser's guides (albedo, specular,
        // roughness) became the reflected world's: they slid with the reflection as the view moved, and DLSS Ray
        // Reconstruction lost the history of everything behind glass - grainy in motion, in the reflection's shapes.
        MaterialInfo seenMaterial = rayLoadMaterial(mainRay);
        MainRay reflection = mainRay;
        resetMainRay(reflection, selectedFirstHit.startBounce);
        reflection.origin = g_glassReflectionOrigin + g_glassReflectionDirection * 0.002;
        reflection.direction = g_glassReflectionDirection;
        reflection.radiance = vec3(0.0);
        reflection.throughput = g_glassReflectionWeight;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        raySetInWater(reflection, (g_glassReflectionMedium & 1u) != 0u);
        raySetInGlass(reflection, (g_glassReflectionMedium & 2u) != 0u);
        raySetGlassParentWater(reflection, (g_glassReflectionMedium & 4u) != 0u);
#endif
        reflection.seed = xxhash32(uvec3(saved.seed, 0x51ED27u, worldUBO.seed));
        raySetLobeType(reflection, 1u);
        raySetCameraPath(reflection, false);
        raySetIgnoreWaterSelf(reflection, false);
        bool reflectionHit;
        float reflectionDistance;
        traceSecondary(reflection, selectedFirstHit.startBounce, 0,
#    if SHARC_QUERY && USE_SHARC
                      sharcParameters, sharcDebugBounceCount, sharcDebugCacheHit,
#    endif
                      reflectionHit, reflectionDistance);
        glassReflectionRadiance = mainRay.radiance;
        if (any(isnan(glassReflectionRadiance)) || any(isinf(glassReflectionRadiance))) {
            glassReflectionRadiance = vec3(0.0);
        }
        mainRay = saved;
        rayStoreMaterial(mainRay, seenMaterial.albedoValue, seenMaterial.f0, seenMaterial.roughness,
                         seenMaterial.metallic, seenMaterial.transmission, seenMaterial.ior, seenMaterial.emission);
    }
