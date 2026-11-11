// Part of main() in world/world.rgen: included there, in the middle of the function, and nowhere else. It is not a
// file of functions and has no include guard: it runs inside main() and shares its local variables with the parts
// before and after it.
//
// The end of the pixel: night vision and Bedrock's ambient, the emission channel, the fog record for the fog pass,
// the SHaRC debug modes, the glow outline of entities and the picture itself.

    // Night vision brightens Minecraft's lightmap, and a path tracer reads no lightmap, so the effect did nothing
    // at all: a cave stayed as black with it as without. The surface the camera sees gets a flat term of its own
    // colour instead, which is how the lightmap's floor reads in vanilla - everything visible, washed out, no
    // shadows of its own. It goes into the emission channel, which the NRD compose reads and leaves undenoised,
    // and onto the radiance below, which is what DLSS Ray Reconstruction is handed; neither pass reads both.
    vec3 nightVisionAmbient = vec3(0.0);
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // Bedrock RTX's ambient, added to the light on the surface in view before the surface's colour multiplies it:
    // 5 a (0.2 + 0.8 (N.V)^1.1) on the scale Bedrock works at (2^bedrockMeter.y), most where the surface faces the
    // camera. a is night vision's strength, with a floor of its own in a dark place: none while the metered level
    // is under 10 stops from 0.75, rising to 0.05 at 14 - a room lit by one torch is seen by a faint, grey glow from
    // where the player stands. (Bedrock RTX's own equation and constants.)
    vec4 bedrockMeter = lightMeter.bedrockMeter;
    if (!hitSky && !useTransparentSplit && bedrockMeter.w == 1.0) {
        float darkFloor = clamp(0.25 * bedrockMeter.x - 2.5, 0.0, 1.0) * 0.05;
        float ambientLevel = skyUBO.nightVision * (1.0 - darkFloor) + darkFloor;
        float facing = pow(max(dot(-firstHitIncomingDirection, firstHitNormal), 0.0), 1.1);
        nightVisionAmbient = firstHitMat.albedoValue.rgb * firstHitMat.albedoValue.a * (1.0 - firstHitMat.metallic) *
                             (5.0 * ambientLevel * (0.2 + 0.8 * facing) * exp2(-bedrockMeter.y));
    }
#else
    if (skyUBO.nightVision > 0.0 && !hitSky) {
        nightVisionAmbient = skyUBO.nightVision * NIGHT_VISION_AMBIENT * firstHitMat.albedoValue.rgb *
                             firstHitMat.albedoValue.a;
    }
#endif
#if VPT_NRD_SIGNALS
    // The glow the surface was seen with, as the closest hit added it.
    if (nrdSplit) { firstHitEmission = nrdEmission; }
#endif
    firstHitEmission += nightVisionAmbient;
    if (skyUBO.hasBlindnessOrDarkness > 0 && composeFogTransmittance >= 0.0) {
        // The emission channel skips the fog above; without this, glowing blocks shone through the dark.
        firstHitEmission *= composeFogTransmittance;
    }
    if (!psrHitSky) { firstHitEmission += glassReflectionRadiance; }
    imageStore(firstHitBaseEmissionImage, ivec2(pixel), vec4(firstHitEmission, 1.0));
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    if (composeFogIsRgb) { composeFogTransmittance = -2.0; }
#endif
    bool applyVolumetricCompose =
        useVolumetricFog && !raySkipFog(mainRay) && skyUBO.cameraSubmersionType != 1 && !cameraInMedium &&
        worldUBO.skyType == 1 && skyUBO.hasBlindnessOrDarkness == 0;
#if VPT_FROXEL_ACTIVE
    if (applyVolumetricCompose) {
        // The march through the fog's world grids runs after this pass (froxel/march.comp), from what is kept here,
        // and folds the fog into this pixel's radiance and composed fog as the per-pixel march below does.
        float volumetricDistance = surfaceDepth == INF_DISTANCE ? volumetricMediumMaxDistance() : surfaceDepth;
        float biomeReach =
            biomeFogReach(orgDirection, surfaceDepth == INF_DISTANCE, surfaceDepth == INF_DISTANCE ? 0.0 : surfaceDepth);
        writeFogMarchRecord(ivec2(pixel), volumetricDistance, surfaceDepth == INF_DISTANCE, biomeReach,
                            fogGlassTint(volumetricDistance), useTransparentSplit);
    }
#else
    if (applyVolumetricCompose) {
        // Only as far as the first surface, as the biome haze: a ray carried on off water or glass crosses its own
        // air, and marching the whole reflected or refracted length hid every pool under a white wall of fog.
        float volumetricDistance = surfaceDepth == INF_DISTANCE ? volumetricMediumMaxDistance() : surfaceDepth;
        vec3 volumetricTransmittance;
        vec3 volumetricLight;
        float biomeReach =
            biomeFogReach(orgDirection, surfaceDepth == INF_DISTANCE, surfaceDepth == INF_DISTANCE ? 0.0 : surfaceDepth);
        integrateVolumetricFog(eyePos, orgDirection, volumetricDistance, biomeReach, rayInsideBoat(mainRay),
                               volumetricTransmittance, volumetricLight);
        volumetricLight *= fogGlassTint(volumetricDistance);
        mainRay.radiance = mainRay.radiance * volumetricTransmittance + volumetricLight;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0 && VPT_BEDROCK_FOG != 0
        composeFogTransmissionRGB = volumetricTransmittance * (composeFogTransmittance < 0.0 ? 1.0 : composeFogTransmittance);
        composeFogAdditive = (composeFogTransmittance < 0.0 ? vec3(0.0) : composeFogAdditive * volumetricTransmittance) + volumetricLight;
        // -1 means no fog; -2 explicitly selects RGB transmission in compose, preserving legacy packs.
        composeFogTransmittance = -2.0;
#else
        if (!useTransparentSplit) {
            // Folded into whatever fog is already composed (the biome haze) rather than replacing it.
            float volumetricComposeTransmittance = clamp(volumetricLightLuminance(volumetricTransmittance), 0.0, 1.0);
            if (composeFogTransmittance < 0.0) {
                composeFogTransmittance = volumetricComposeTransmittance;
                composeFogAdditive = volumetricLight;
            } else {
                composeFogAdditive = composeFogAdditive * volumetricComposeTransmittance + volumetricLight;
                composeFogTransmittance *= volumetricComposeTransmittance;
            }
        }
#endif
    }
#endif
    bool isFirstHitSpecular = useTransparentSplit ? hasTransparentReflection :
                                                    (isFirstHitNoisy && !isFirstHitDiffuse && !isFirstHitRefraction);
    float specHitDepth = 0.0;
    if (useTransparentSplit) {
        specHitDepth = hasTransparentReflection ?
                           (hasTransparentReflectionSecondHit ? transparentReflectionSecondHitDistance : INF_DISTANCE) :
                           0.0;
    } else {
        specHitDepth = isFirstHitSpecular ? (hasSecondHit ? distFirstToSecond : INF_DISTANCE) : 0.0;
    }
    imageStore(fogImage, ivec2(pixel), vec4(composeFogAdditive, composeFogTransmittance));
#if defined(RADIANTE_DEBUG_VIEW) && RADIANTE_DEBUG_VIEW == 14
    vec3 radianteDebugSharcLight = max(g_radianteDebugSharc, vec3(0.0));
    imageStore(radianteDebugViewImage, ivec2(pixel),
               vec4(radianteDebugDisplay(radianteDebugSharcLight / (1.0 + radianteDebugSharcLight)), 1.0));
#endif
    imageStore(fogTransmissionImage, ivec2(pixel), vec4(composeFogTransmissionRGB, 1.0));
    imageStore(postStarCloudTransmittanceImage, ivec2(pixel), vec4(postStarCloudTransmittance));
    imageStore(specularHitDepthImage, ivec2(pixel), vec4(specHitDepth, vec3(1.0)));
#    if SHARC_QUERY && USE_SHARC
    if (sharcConfig.debugMode == 1u && !psrHitSky) {
        vec3 debugPosWorld = psrHitPosition + vec3(worldUBO.cameraPos.xyz);
        vec3 debugNormal = vec3(0.0, 1.0, 0.0);
        mainRay.radiance = HashGridDebugColoredHash(debugPosWorld, debugNormal, sharcParameters.gridParameters);
    } else if (sharcConfig.debugMode == 2u) {
        mainRay.radiance =
            HashGridDebugOccupancy(uvec2(pixel), uvec2(imageSize(outputImage)), sharcParameters.hashMapData);
    } else if (sharcConfig.debugMode == 3u) {
        float t = clamp(float(sharcDebugBounceCount) / max(float(VPT_NUM_RAY_BOUNCES), 1.0), 0.0, 1.0);
        vec3 heat = mix(vec3(0.0, 0.8, 0.0), vec3(1.0, 0.0, 0.0), t);
        if (sharcDebugCacheHit) { heat = vec3(0.0, 0.5, 1.0); }
        mainRay.radiance = heat;
    }
#    endif
    vec3 glowColor;
    if (worldUBO.hasGlowOutline != 0u && glowOutline(pixelCenter, resolution, eyePos, glowColor)) {
        // Written over everything the pixel held, into both what DLSS Ray Reconstruction reads and the NRD
        // compose's clear channel, which it passes through untouched.
        vec3 glowRadiance = glowColor * GLOW_OUTLINE_RADIANCE;
        imageStore(firstHitClearImage, ivec2(pixel), vec4(glowRadiance, 1.0));
        imageStore(diffuseAlbedoImage, ivec2(pixel), vec4(glowColor, 0.0));
        imageStore(specularAlbedoImage, ivec2(pixel), vec4(0.0));
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        imageStore(fogImage, ivec2(pixel), vec4(0.0, 0.0, 0.0, -1.0));
#endif
        imageStore(outputImage, ivec2(pixel), vec4(glowRadiance, 1.0));
#if VPT_FROXEL_ACTIVE && !(SHARC_UPDATE && USE_SHARC)
        clearFogMarchRecord(ivec2(pixel));
#endif
        return;
    }
#if VPT_HAND_PRIMARY_BOUNDS == 2
    // Diagnostic build only; do not use this view for image quality comparisons.
    mainRay.radiance = inHandBounds ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
#elif VPT_HAND_PRIMARY_BOUNDS == 3
    // Trace every hand ray: a red actual hand hit falsifies conservativeness, even where mode 1 would skip it.
    mainRay.radiance = isHand != 0 ? (predictedHandBounds ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)) : vec3(0.0);
#endif
    imageStore(outputImage, ivec2(pixel), vec4(mainRay.radiance + nightVisionAmbient, 1.0));
