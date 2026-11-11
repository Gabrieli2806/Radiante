// Part of main() in world/world.rgen: included there, in the middle of the function, and nowhere else. It is not a
// file of functions and has no include guard: it runs inside main() and shares its local variables with the parts
// before and after it.
//
// What the denoiser and the upscaler are handed besides the picture: the albedo guide of see-through glass, the
// guides faded into the medium the camera is in, and the first hit's light split into the images each pipeline reads
// (NRD's signals, or diffuse, specular, clear and refraction for DLSS Ray Reconstruction).

    if (useTransparentSplit) {
        // The denoiser's albedo guide for see-through glass is the colour light takes through it. Its texture albedo
        // times alpha was nearly black for clear glass and panes (alpha 0 where they are clear), and DLSS Ray
        // Reconstruction, dividing the picture by that guide, drew everything seen through them dark.
        vec3 transmissionGuide = clamp(viewGlassTint(firstHitMat.albedoValue.rgb, firstHitMat.albedoValue.a), 0.0, 1.0);
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        // Water has no surface colour filter; absorption is integrated in its medium.
        if (firstHitWaterSurface) { transmissionGuide = vec3(1.0); }
#endif
        imageStore(diffuseAlbedoImage, pixel, vec4(transmissionGuide, 0.0));
        firstHitEmission += transparentFogAdditive;
        mainRay.radiance = transparentReflectionRadiance + transparentRefractionRadiance + firstHitEmission;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        // This replaces the radiance put together earlier: the glass's own reflection, traced apart, is put back
        // where the surface behind a pane is water. NRD takes it once through the emission image below.
        mainRay.radiance += glassReflectionRadiance;
#endif
        composeFogAdditive = vec3(0.0);
        composeFogTransmittance = -1.0;
    }

    // In lava or powder snow the denoiser's guides fade into the medium as the picture does. They were written for the
    // surface the ray hit, behind it, and DLSS Ray Reconstruction rebuilt that surface's texture and edges from them
    // through a medium that hid it. Where the medium hides everything the guides are the medium's: its colour, scaled
    // as the sky's guide is (by its brightest channel), no specular and no normal.
    if (cameraInMedium) {
        float mediumLevel = max(max(cameraMediumLight.r, max(cameraMediumLight.g, cameraMediumLight.b)), 1e-6);
        vec3 mediumGuide = clamp(cameraMediumLight / mediumLevel, 0.0, 1.0);
        vec3 surfaceGuide = psrHitSky ? mediumGuide : firstHitMat.albedoValue.rgb * firstHitMat.albedoValue.a;
        vec3 surfaceSpecular = psrHitSky ? vec3(0.0) : firstHitMat.f0;
        vec3 shown = cameraMediumTransmittance;
        imageStore(diffuseAlbedoImage, pixel, vec4(mix(mediumGuide, surfaceGuide, shown), 0.0));
        imageStore(specularAlbedoImage, pixel, vec4(surfaceSpecular * shown, 0.0));
        if (volumetricLightLuminance(shown) < 0.01) { imageStore(normalRoughnessImage, pixel, vec4(0.0)); }
    }

#if VPT_NRD_SIGNALS
    bool nrdSplit = false;
#endif
    if (!psrHitSky && isFirstHitNoisy) {
        if (useTransparentSplit) {
            imageStore(firstHitDiffuseDirectLightImage, ivec2(pixel), vec4(vec3(0.0), 1.0));
            imageStore(firstHitDiffuseIndirectLightImage, ivec2(pixel), vec4(vec3(0.0), 1.0));
            imageStore(firstHitSpecularImage, ivec2(pixel), vec4(storedRadiance(transparentReflectionRadiance), 1.0));
            imageStore(firstHitClearImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
            imageStore(firstHitRefractionImage, ivec2(pixel),
                       vec4(storedRadiance(transparentRefractionRadiance), hasTransparentRefraction ? 1.0 : -1.0));
#if VPT_NRD_SIGNALS
        } else if (isFirstHitDiffuse || !isFirstHitTransparent) {
            // NRD's signals, apart (see clearNrdRecord): the glow goes to the channel NRD leaves as it is (the emission
            // image, below), where it stays sharp - in the diffuse signal it was blurred into a blob and then added a
            // second time; the direct light to the diffuse signal whichever lobe the bounce took (it used to go
            // wholly to the specular signal one frame in ten, which divided it by a reflectance of a few hundredths:
            // sparks on every lit wall); and the bounce's light to the signal of its lobe, with its length as the
            // hit distance NRD's diffuse filter is scaled by (none when it took the other lobe: NRD reconstructs it
            // from the neighbours).
            vec3 nrdBounce = max(mainRay.radiance - nrdFirstHitDirect - nrdEmission, vec3(0.0));
            float nrdHitDistance = hasSecondHit ? distFirstToSecond : NRD_MISS_HIT_DISTANCE;
            imageStore(firstHitDiffuseDirectLightImage, ivec2(pixel), vec4(storedRadiance(nrdFirstHitDirect), 1.0));
            imageStore(firstHitDiffuseIndirectLightImage, ivec2(pixel),
                       vec4(storedRadiance(isFirstHitDiffuse ? nrdBounce : vec3(0.0)),
                            isFirstHitDiffuse ? nrdHitDistance : 0.0));
            imageStore(firstHitSpecularImage, ivec2(pixel),
                       vec4(storedRadiance(isFirstHitDiffuse ? vec3(0.0) : nrdBounce), 1.0));
            imageStore(firstHitClearImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
            imageStore(firstHitRefractionImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
            nrdSplit = true;
#endif
        } else if (isFirstHitDiffuse) {
            imageStore(firstHitDiffuseDirectLightImage, ivec2(pixel), vec4(storedRadiance(firstHitDirectRadiance), 1.0));
            imageStore(firstHitDiffuseIndirectLightImage, ivec2(pixel), vec4(storedRadiance(firstHitIndirectRadiance), 1.0));
            imageStore(firstHitSpecularImage, ivec2(pixel), vec4(vec3(0.0), 1.0));
            imageStore(firstHitClearImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
            imageStore(firstHitRefractionImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
        } else if (isFirstHitRefraction) {
            imageStore(firstHitDiffuseDirectLightImage, ivec2(pixel), vec4(vec3(0.0), 1.0));
            imageStore(firstHitDiffuseIndirectLightImage, ivec2(pixel), vec4(vec3(0.0), 1.0));
            imageStore(firstHitSpecularImage, ivec2(pixel), vec4(vec3(0.0), 1.0));
            imageStore(firstHitClearImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
            imageStore(firstHitRefractionImage, ivec2(pixel), vec4(storedRadiance(mainRay.radiance), 1.0));
        } else {
            imageStore(firstHitDiffuseDirectLightImage, ivec2(pixel), vec4(vec3(0.0), 1.0));
            imageStore(firstHitDiffuseIndirectLightImage, ivec2(pixel), vec4(vec3(0.0), 1.0));
            imageStore(firstHitSpecularImage, ivec2(pixel), vec4(storedRadiance(mainRay.radiance), 1.0));
            imageStore(firstHitClearImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
            imageStore(firstHitRefractionImage, ivec2(pixel), vec4(vec3(0.0), isFirstHitTransparent ? -1.0 : 0.0));
        }
    } else {
        imageStore(firstHitDiffuseDirectLightImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
        imageStore(firstHitDiffuseIndirectLightImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
        imageStore(firstHitSpecularImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
        imageStore(firstHitClearImage, ivec2(pixel), vec4(storedRadiance(mainRay.radiance), 1.0));
        imageStore(firstHitRefractionImage, ivec2(pixel), vec4(vec3(0.0), 0.0));
    }
