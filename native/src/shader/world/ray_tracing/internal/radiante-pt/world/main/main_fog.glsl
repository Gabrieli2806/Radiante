// Part of main() in world/world.rgen: included there, in the middle of the function, and nowhere else. It is not a
// file of functions and has no include guard: it runs inside main() and shares its local variables with the parts
// before and after it.
//
// What lies between the camera and the surface seen: blindness and darkness, the water, lava or powder snow the
// camera is in, the game's fog or the volumetric march, the biome's haze and the air's own over the distance. It
// changes mainRay.radiance and leaves in composeFog* what the fog pass composes later.

    if (!hitSky) { mainRay.radiance += glassReflectionRadiance; }
    if (isnan(mainRay.radiance.r) || isinf(mainRay.radiance.r)) { mainRay.radiance = vec3(0.0, 0.0, 0.0); }
    if (isnan(mainRay.radiance.g) || isinf(mainRay.radiance.g)) { mainRay.radiance = vec3(0.0, 0.0, 0.0); }
    if (isnan(mainRay.radiance.b) || isinf(mainRay.radiance.b)) { mainRay.radiance = vec3(0.0, 0.0, 0.0); }

    float densityAir = 0.0000;
    float densityFog = 2.0 / (worldUBO.fogEnd - worldUBO.fogStart);
    bool useVolumetricFog = VPT_VOLUMETRIC_LIGHT_MODE == VPT_VOLUMETRIC_LIGHT_MODE_VOLUMETRIC;
    float transparentFogTransmittance = 1.0;
    vec3 transparentFogAdditive = vec3(0.0);
    if (skyUBO.hasBlindnessOrDarkness > 0) {
        // Blindness and Darkness: vanilla's fog closes in to a few blocks (fogStart/fogEnd follow the effect's
        // pulse) and everything past it goes black. Applied even where fog is otherwise skipped.
        // Linear from fogStart to fogEnd as vanilla's fog is; the sky (and, while the effect is still strong, all of
        // it) goes black with the effect's strength, so it fades back in as the fog pulls away instead of popping.
        float strength = skyUBO.fogControls.x;
        // Darkness's pulse also pulls the fog in: at each beat the wall closes to about half its distance and
        // then eases back out, so the dark visibly breathes toward the player.
        float pulseReach = 1.0 - 0.5 * skyUBO.fogControls.y;
        float effectFogStart = worldUBO.fogStart * pulseReach;
        float effectFogEnd = worldUBO.fogEnd * pulseReach;
        float fogAmount = surfaceDepth == INF_DISTANCE ?
            1.0 : clamp((surfaceDepth - effectFogStart) / max(effectFogEnd - effectFogStart, 0.01), 0.0, 1.0);
        float transmittance = 1.0 - fogAmount * smoothstep(0.0, 0.3, strength);
        // Darkness's pulse: vanilla takes it off the lightmap, so everything lit dims at once and the world blinks.
        transmittance *= 1.0 - 0.95 * skyUBO.fogControls.y;
        // Black, not the fog colour: that one is still lifted by the sky and came out grey.
        vec3 additive = vec3(0.0);
        if (useTransparentSplit) {
            transparentReflectionRadiance *= transmittance;
            transparentRefractionRadiance *= transmittance;
            transparentFogAdditive = additive;
            transparentFogTransmittance = transmittance;
        } else {
            mainRay.radiance = mainRay.radiance * transmittance + additive;
            composeFogTransmittance = transmittance;
            composeFogAdditive = additive;
        }
    } else if (!raySkipFog(mainRay)) {
        if (skyUBO.cameraSubmersionType == 1 && waterMediumActive()) {
            // A Bedrock pack's water: the camera's stretch of it absorbs and scatters like every other one. Its
            // first stretch only: past the first surface the hit shaders fog the water, and fogging up to the
            // first solid surface here counted the water behind a pane or a particle twice. A ray that hits nothing
            // has water without a far side (it used to count as none, showing the bright open sky deep under water).
            float waterDistance = selectedFirstHit.cameraWaterDistance;
            vec3 waterTransmittance;
            vec3 waterScatter;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
            waterMediumSegmentBedrock(waterDistance >= INF_DISTANCE || VPT_MEDIUM_DISTANCE_CLAMP == 0 ?
                                          waterDistance : min(waterDistance, 50.0),
                                      VPT_WATER_DENSITY, vec3(0.0), orgDirection, waterTransmittance, waterScatter);
#else
            // A pool is clearer than the sea its biome's coefficients describe (waterBodyShallowness).
            float cameraWaterDensity = VPT_WATER_DENSITY * mix(1.0, UNDERWATER_SMALL_BODY_DENSITY, waterBodyShallowness());
            if (waterDistance >= INF_DISTANCE) {
                waterMediumSegmentUnclamped(waterDistance, cameraWaterDensity, waterTransmittance, waterScatter);
            } else {
                waterMediumSegment(waterDistance, cameraWaterDensity, waterTransmittance, waterScatter);
            }
            // By day what lies behind the water was lit by daylight that came down through the water above the camera
            // first (underwaterDaylightColumn); the water's own glow already is (waterMediumIncomingLight).
            waterTransmittance *= underwaterDaylightColumn();
#endif
            // Lit by the sky above: the camera's water under a roof or in a cave does not glow with it.
            waterScatter *= mix(0.35, 1.0, volumetricSkyVisibility(eyePos, false));
            waterScatter += waterGodRays(eyePos, orgDirection, waterDistance);
            float waterComposeTransmittance = clamp(volumetricLightLuminance(waterTransmittance), 0.0, 1.0);
            if (useTransparentSplit) {
                transparentReflectionRadiance *= waterTransmittance;
                transparentRefractionRadiance *= waterTransmittance;
                transparentFogAdditive = waterScatter;
                transparentFogTransmittance = waterComposeTransmittance;
            } else {
                mainRay.radiance = mainRay.radiance * waterTransmittance + waterScatter;
                composeFogTransmittance = waterComposeTransmittance;
                composeFogAdditive = waterScatter;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
                composeFogTransmissionRGB = waterTransmittance;
                composeFogIsRgb = true;
#endif
            }
        }
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        if (skyUBO.cameraSubmersionType == 1 && !waterMediumActive()) {
#else
        if (skyUBO.cameraSubmersionType == 1) {
#endif
            // How far one sees under water: past a few dozen blocks everything fades into the water's own colour
            // (a sea blue), lit as the water around the camera is. Without it a clear ocean
            // showed its floor and the shore far away, as if through air.
            // Each biome's water has its own reach and colour (skyUBO.waterFog, from BiomeAmbiance): clear and
            // bright by a beach, dark and close in the deep ocean, a murk in a swamp. One water for all of them
            // with the option off.
            bool biomeWater = skyUBO.waterFog.a > 0.5;
            float visibility = biomeWater ? skyUBO.waterFog.a : UNDERWATER_VISIBILITY;
            // a clear sea blue for plain water (vanilla's water fog is nearly navy)
            vec3 fogTint = biomeWater ? max(skyUBO.waterFog.rgb, vec3(0.0)) : UNDERWATER_FOG_TINT;
            // The biome's water is the sea's. In a pool or a pond there is too little water for its murk to gather:
            // the view reaches further and the water it fades into is lighter, while the sea keeps its look.
            float shallow = waterBodyShallowness();
            visibility *= mix(1.0, UNDERWATER_SMALL_BODY_VISIBILITY, shallow);
            fogTint *= mix(1.0, UNDERWATER_SMALL_BODY_BRIGHTNESS, shallow);
#ifndef RADIANTE_HOOK_UNDERWATER_FOG_DEFAULT
            // A pack's water seen from inside (hooks/underwater_fog.glsl).
            hookUnderwaterFog(fogTint, visibility);
            fogTint = max(fogTint, vec3(0.0));
            visibility = max(visibility, 0.01);
#endif
            // Only the water the view crosses fogs it: looking up out of a pool at the sky or the shore, the stretch
            // past the surface is air, which the fog counted as water (a pool's surface was a wall of sea blue). The
            // first stretch ends at the water's surface or whatever stands in the water.
            float seen = surfaceDepth == INF_DISTANCE || surfaceDepth <= 0.0 ? visibility * 4.0 : surfaceDepth;
            seen = min(seen, selectedFirstHit.cameraWaterDistance);
            float clearness = exp(-seen / visibility);
            // How much of the sky lights the water around the camera. With a biome's water it is the sky light over
            // the water's surface there (skyUBO.waterAlbedo.a, eased by BiomeAmbiance); one ray straight up from the
            // eye went black under a lily pad or a branch and lit again a step on, and a swamp was mostly black.
            float skyOverWater =
                biomeWater ? clamp(skyUBO.waterAlbedo.a, 0.0, 1.0) : volumetricSkyVisibility(eyePos, false);
            float ambient = dot(texture(skyFull, vec3(0.0, 1.0, 0.0)).rgb, vec3(0.2126, 0.7152, 0.0722)) *
                            mix(0.2, 1.0, skyOverWater);
            // The deeper the camera, the less of the sky's light is left in the water around it: the colour the
            // view fades into darkens on the way down, sooner in water one sees less far in.
            // By day that light has crossed the water above the camera at the water's own absorption (Beer-Lambert),
            // which darkens it much sooner than the dimming kept for the night, whose look it leaves as it was.
            float depthDimming = biomeWater ? exp(-waterCameraDepth() * UNDERWATER_DEPTH_DIMMING / visibility) : 1.0;
            float dayColumn = dot(underwaterDaylightColumn(), vec3(0.2126, 0.7152, 0.0722));
            ambient *= min(depthDimming, dayColumn);
            vec3 fogLight = fogTint * ambient * UNDERWATER_FOG_LIGHT;
            if (useTransparentSplit) {
                transparentReflectionRadiance *= clearness;
                transparentRefractionRadiance *= clearness;
                transparentFogAdditive = transparentFogAdditive * clearness + fogLight * (1.0 - clearness);
                transparentFogTransmittance *= clearness;
            } else {
                mainRay.radiance = mainRay.radiance * clearness + fogLight * (1.0 - clearness);
                composeFogAdditive = composeFogAdditive * clearness + fogLight * (1.0 - clearness);
                composeFogTransmittance = (composeFogTransmittance < 0.0 ? 1.0 : composeFogTransmittance) * clearness;
            }
        }
        if (skyUBO.cameraSubmersionType == 1 && waterMediumActive()) {
        } else if (skyUBO.cameraSubmersionType == 1) {
            vec3 waterColor = vec3(0.0, 0.48, 0.65);
            vec3 preFogRadiance = mainRay.radiance * waterColor;

            if (useTransparentSplit) {
                applyFog(preFogRadiance, surfaceDepth, worldUBO.fogStart, densityAir, densityFog, waterColor * 0.01,
                         mainRay.radiance, transparentFogTransmittance, transparentFogAdditive);
                transparentReflectionRadiance *= waterColor * transparentFogTransmittance;
                transparentRefractionRadiance *= waterColor * transparentFogTransmittance;
            } else {
                applyFog(preFogRadiance, surfaceDepth, worldUBO.fogStart, densityAir, densityFog, waterColor * 0.01,
                         mainRay.radiance, composeFogTransmittance, composeFogAdditive);
            }
        } else if (cameraInMedium) {
            // The camera in lava or powder snow: a medium of its own, the same in every dimension. Both used to fall
            // through to the dimension's air haze: lava opaque in the Nether only by chance (vanilla's lava fog
            // distance and colour) and see-through in the overworld, powder snow black.
            vec3 mediumTransmittance;
            vec3 mediumLight;
            homogeneousMediumSegment(surfaceDepth, cameraMediumSigmaT, cameraMediumLight, mediumTransmittance,
                                     mediumLight);
            float mediumComposeTransmittance = clamp(volumetricLightLuminance(mediumTransmittance), 0.0, 1.0);
            cameraMediumTransmittance = mediumTransmittance;
            if (useTransparentSplit) {
                transparentReflectionRadiance *= mediumTransmittance;
                transparentRefractionRadiance *= mediumTransmittance;
                transparentFogAdditive = mediumLight;
                transparentFogTransmittance = mediumComposeTransmittance;
            } else {
                mainRay.radiance = mainRay.radiance * mediumTransmittance + mediumLight;
                composeFogTransmittance = mediumComposeTransmittance;
                composeFogAdditive = mediumLight;
            }
        } else {
            if (worldUBO.skyType == 0) {
#ifdef RADIANTE_HOOK_DIMENSION_SKY_DEFAULT
                if (useTransparentSplit) {
                    applyFog(mainRay.radiance, surfaceDepth, worldUBO.fogStart, densityAir, densityFog,
                             worldUBO.fogColor.rgb * NETHER_FOG_SCALE, mainRay.radiance, transparentFogTransmittance,
                             transparentFogAdditive);
                    transparentReflectionRadiance *= transparentFogTransmittance;
                    transparentRefractionRadiance *= transparentFogTransmittance;
                } else {
                    applyFog(mainRay.radiance, surfaceDepth, worldUBO.fogStart, densityAir, densityFog,
                             worldUBO.fogColor.rgb * NETHER_FOG_SCALE, mainRay.radiance, composeFogTransmittance,
                             composeFogAdditive);
                }
#else
                vec3 netherFog = worldUBO.fogColor.rgb * NETHER_FOG_SCALE;
                float netherFogDensity = densityFog;
                hookNetherFog(netherFog, netherFogDensity); // a pack's Nether fog (hooks/dimension_sky.glsl)
                if (useTransparentSplit) {
                    applyFog(mainRay.radiance, surfaceDepth, worldUBO.fogStart, densityAir, netherFogDensity,
                             netherFog, mainRay.radiance, transparentFogTransmittance, transparentFogAdditive);
                    transparentReflectionRadiance *= transparentFogTransmittance;
                    transparentRefractionRadiance *= transparentFogTransmittance;
                } else {
                    applyFog(mainRay.radiance, surfaceDepth, worldUBO.fogStart, densityAir, netherFogDensity,
                             netherFog, mainRay.radiance, composeFogTransmittance, composeFogAdditive);
                }
#endif
            } else if (worldUBO.skyType == 1) {
                if (!useVolumetricFog && !psrHitSky) {
                    vec3 eyeToPSRHitPosition = psrHitPosition - eyePos;
                    eyeToPSRHitPosition.y = 0;
                    float len = length(eyeToPSRHitPosition);

                    vec3 sunDir = celestialSunDirection();
                    vec3 rayDir = orgDirection;
                    float progress = skyUBO.rainGradient;
                    vec3 rainyRadiance = mix(vec3(0.0), vec3(0.1), smoothstep(-0.3, 0.3, sunDir.y));
                    vec3 sunnyRadiance = texture(skyFull, rayDir).rgb;
                    vec3 fogRadiance = mix(sunnyRadiance, rainyRadiance, progress);

                    if (useTransparentSplit) {
                        applyFog(mainRay.radiance, len, worldUBO.fogStart, densityAir, densityFog, fogRadiance,
                                 mainRay.radiance, transparentFogTransmittance, transparentFogAdditive);
                        transparentReflectionRadiance *= transparentFogTransmittance;
                        transparentRefractionRadiance *= transparentFogTransmittance;
                    } else {
                        applyFog(mainRay.radiance, len, worldUBO.fogStart, densityAir, densityFog, fogRadiance,
                                 mainRay.radiance, composeFogTransmittance, composeFogAdditive);
                    }
                }
            } else if (worldUBO.skyType == 2 && surfaceDepth != INF_DISTANCE) {
                // The End's sky is its fog colour already (see the miss shader); fogging it again washed its
                // stars and gradient out to a flat sheet. The islands fade into the same violet.
                vec3 endFog = dot(max(worldUBO.fogColor.rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722)) *
                              vec3(1.3, 0.8, 1.85) * 0.15;
                float endFogDensity = densityFog;
#ifndef RADIANTE_HOOK_DIMENSION_SKY_DEFAULT
                hookEndFog(endFog, endFogDensity); // a pack's End fog (hooks/dimension_sky.glsl)
#endif
                if (useTransparentSplit) {
                    applyFog(mainRay.radiance, surfaceDepth, worldUBO.fogStart, densityAir, endFogDensity,
                             endFog, mainRay.radiance, transparentFogTransmittance,
                             transparentFogAdditive);
                    transparentReflectionRadiance *= transparentFogTransmittance;
                    transparentRefractionRadiance *= transparentFogTransmittance;
                } else {
                    applyFog(mainRay.radiance, surfaceDepth, worldUBO.fogStart, densityAir, endFogDensity,
                             endFog, mainRay.radiance, composeFogTransmittance, composeFogAdditive);
                }
            }
        }
    }

    // With volumetric fog on, the overworld's biome fog is part of that march instead (see integrateVolumetricFog).
    if (!raySkipFog(mainRay) && skyUBO.hasBlindnessOrDarkness == 0 && biomeHazeActive()
        && !(useVolumetricFog && worldUBO.skyType == 1)) {
        float hazeTransmittance;
        vec3 hazeAdditive;
        // Only as far as the first surface: a ray carried on off water or glass is fogged on its own way (the miss
        // shader fogs the sky it ends in). Fogging the whole length at once hid the bottom of every pool that
        // reflected the sky.
        bool hazeHitSky = surfaceDepth == INF_DISTANCE;
        biomeHaze(orgDirection, hazeHitSky, hazeHitSky ? 0.0 : surfaceDepth, hazeTransmittance,
                  hazeAdditive);
        hazeAdditive *= fogGlassTint(hazeHitSky ? INF_DISTANCE : surfaceDepth);
        if (useTransparentSplit) {
            transparentReflectionRadiance *= hazeTransmittance;
            transparentRefractionRadiance *= hazeTransmittance;
            transparentFogAdditive = transparentFogAdditive * hazeTransmittance + hazeAdditive;
            transparentFogTransmittance *= hazeTransmittance;
        } else {
            mainRay.radiance = mainRay.radiance * hazeTransmittance + hazeAdditive;
            if (composeFogTransmittance < 0.0) {
                composeFogTransmittance = hazeTransmittance;
                composeFogAdditive = hazeAdditive;
            } else {
                composeFogAdditive = composeFogAdditive * hazeTransmittance + hazeAdditive;
                composeFogTransmittance *= hazeTransmittance;
            }
        }
    }

    // The air's own haze over the distance (aerialPerspective, common/biome_haze.glsl), in every fog mode: far terrain,
    // Distant Horizons' too, fades towards the sky behind it. Not the sky itself, which is that colour already.
    if (!raySkipFog(mainRay) && skyUBO.hasBlindnessOrDarkness == 0 && worldUBO.skyType == 1 &&
        skyUBO.cameraSubmersionType == 3 && !cameraInMedium && surfaceDepth != INF_DISTANCE) {
        vec3 aerialTransmittance3;
        vec3 aerialAdditive;
        // Radiante's own volumetric air (Fog Style, with Bedrock's fog volume off) hazes the distance already:
        // the clear air only makes up what it lacks.
        float marchedAir = 0.0;
#if !(defined(VPT_BEDROCK_FOG) && VPT_BEDROCK_FOG != 0)
        if (useVolumetricFog) {
            vec3 airSigmaS;
            vec3 airSigmaT;
            volumetricAirMedium(0.0, airSigmaS, airSigmaT);
            marchedAir = volumetricLightLuminance(airSigmaT) * volumetricAirDensity() * 0.45 *
                         max(skyUBO.fogControls.z, 0.0);
        }
#endif
        aerialPerspective(orgDirection, surfaceDepth, marchedAir, aerialTransmittance3, aerialAdditive);
        float aerialTransmittance = aerialTransmittance3.g;
        aerialAdditive *= fogGlassTint(surfaceDepth);
        if (aerialTransmittance < 1.0) {
            if (useTransparentSplit) {
                transparentReflectionRadiance *= aerialTransmittance;
                transparentRefractionRadiance *= aerialTransmittance;
                transparentFogAdditive = transparentFogAdditive * aerialTransmittance + aerialAdditive;
                transparentFogTransmittance *= aerialTransmittance;
            } else {
                mainRay.radiance = mainRay.radiance * aerialTransmittance + aerialAdditive;
                if (composeFogTransmittance < 0.0) {
                    composeFogTransmittance = aerialTransmittance;
                    composeFogAdditive = aerialAdditive;
                } else {
                    composeFogAdditive = composeFogAdditive * aerialTransmittance + aerialAdditive;
                    composeFogTransmittance *= aerialTransmittance;
                }
            }
        }
    }
