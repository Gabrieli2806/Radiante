#ifndef VPT_PT_GLASS_GLSL
#define VPT_PT_GLASS_GLSL

// Glass seen at a hit: in Radiante PT every blended block (glass, ice, slime, honey) is refracted
// through and coloured face by face; otherwise the clear part of glass and panes is seen straight through with a
// Fresnel reflection. Moved out of the default closest-hit shader unchanged; the hit is read through g_hit. Each
// function returns true when it finished the hit as a pass-through.


// The colour one face of see-through glass gives the view through it in Radiante PT. Bedrock RTX filters
// at every face the view crosses, by the colour as light times how clear the texel is (the lower half of the alpha
// range counts as fully clear). A Bedrock pack paints its glass for exactly that: pale, and filtered twice through a
// block it comes out as deep as the glass looks. Glass painted for vanilla's blending (vanilla's own, a Java pack's)
// is far darker, and by that rule a stained window let almost nothing through: it filters by its colour as stored,
// once for the whole block - half at each of the two faces.
vec3 bedrockGlassFaceFilter(vec3 stored, float alpha, bool authored) {
    if (authored) { return clamp(viewGlassTintBedrock(vptSurfaceColour(stored), alpha), vec3(0.0), vec3(1.0)); }
    return sqrt(clamp(viewGlassTintBedrock(stored, alpha), vec3(0.0), vec3(1.0)));
}

bool ptBedrockGlassHit(uint textureID,
                       vec2 textureUV,
                       TextureMapEntry textureMap,
                       vec4 colorLayerValue,
                       vec3 colorLayer,
                       vec3 windingNormal,
                       vec3 baseGeoNormal,
                       uint bounce) {
    // Glass, and the other blocks Bedrock draws blended (ice, slime, honey): the camera sees through their
    // see-through texels, bent at each face by the glass's index, coloured by each face it crosses, with the
    // world reflected on the face it enters by.
    if (g_hitIsGlass || radianteIsBlend(g_hit.packedData)) {
        vec4 texel = sampleTexture(textures[nonuniformEXT(textureID)], textureUV, 0.0, false);
        float alpha = clamp(texel.a * colorLayerValue.a, 0.0, 1.0);
        bool hidden = !radianteIsHeld(g_hit.packedData) && seamlessGlassHidesFace(g_hit.p0.pos, g_hit.p1.pos, g_hit.p2.pos, g_hit.packedData);
        // What the camera sees: straight on, or off and through the surfaces before (a mirror, the water), as
        // Bedrock's view goes on through one surface after another.
        bool cameraView = bounce == 0u || rayCameraPath(mainRay);
        bool viewRay = cameraView || g_viewBounce;
        bool inside = rayInGlass(mainRay);
        // A view goes into glass only through a face turned towards it and comes out only through one turned
        // away: the faces in between (the inner cube of a slime or honey block) are not there for it, nor is the
        // far shell once it is out. Crossed one by one, each bent the view again and coloured it twice over.
        // Not what is held: its faces may be wound either way. (The any-hit shader leaves them out already.)
        if (viewRay && !radianteIsHeld(g_hit.packedData) &&
            inside == (dot(windingNormal, g_hit.rayDir) < 0.0)) {
            hidden = true;
        }
        if (!viewRay && (hidden || alpha < 0.999)) {
            // Light on its way to a surface: it crosses glass straight on and takes the colour of each face, as
            // Bedrock's bounce rays pass every blended surface in their way and shade none. Shaded as surfaces, a
            // pack's smoothest texels (the specks of its ice) threw sparks of sunlight on whatever lay behind them.
            if (!hidden) {
                mainRay.throughput *= bedrockGlassFaceFilter(texel.rgb * colorLayer, alpha,
                                                             bedrockAuthoredTexel(textureMap, textureUV));
            }
            mainRay.hitT = g_hit.hitT;
            mainRay.coneWidth += mainRay.hitT * mainRay.coneSpread;
            mainRay.origin = g_hit.rayOrigin + g_hit.rayDir * (g_hit.hitT + PASS_THROUGH_STEP);
            ptClearReflectionRequest();
            mainRay.hasPrevScenePos = 0u;
            ptStoreMaterial(vec4(1.0), vec3(0.04), 1.0, 0.0, 1.0, 1.0, 0.0);
            ptStoreAux(vec2(0.0));
            raySetNoisy(mainRay, false);
            raySetContinue(mainRay, true);
            raySetPassThrough(mainRay, true);
            raySetStop(mainRay, false);
            return true;
        }
        if (hidden || alpha < 0.999) {
            vec3 incident = normalize(g_hit.rayDir);
            bool water = rayInWater(mainRay);
            bool parentWater = rayGlassParentWater(mainRay);
            // As far as the texel is opaque (the upper half of its alpha, as Bedrock reads it) it is a lit surface
            // of its own colour over what shows through the rest: ice over deep water is frosted blue, not black
            // glass. Bedrock shows it so on the first face the camera's view crosses; on the faces after that the
            // view only refracts.
            float opacity = clamp(alpha * 2.0 - 1.0, 0.0, 1.0);
            if (!hidden && !inside && opacity > 0.0 && cameraView && !rayCrossedGlass(mainRay)) {
                mainRay.radiance += bedrockBlendedFaceLight(g_hit.rayOrigin + incident * g_hit.hitT, baseGeoNormal,
                                                            -incident, vptSurfaceColour(texel.rgb * colorLayer),
                                                            opacity);
            }
            float n1 = inside ? 1.5 : (water ? 1.33 : 1.0);
            float n2 = inside ? (parentWater ? 1.33 : 1.0) : 1.5;
            float eta = n1 / n2;
            vec3 transmitted = hidden ? incident : refract(incident, baseGeoNormal, eta);
            bool tir = dot(transmitted, transmitted) < 1e-8;
            vec3 reflected = reflect(incident, baseGeoNormal);
            float fresnel = hidden ? 0.0 : vptInterfaceFresnel(abs(dot(incident, baseGeoNormal)), eta);
            vec3 reflectionWeight = !tir && !hidden ? mainRay.throughput * fresnel : vec3(0.0);
            vec3 filterColour = hidden ? vec3(1.0) :
                bedrockGlassFaceFilter(texel.rgb * colorLayer, alpha, bedrockAuthoredTexel(textureMap, textureUV));
            // Reflected back whole inside the glass, the view takes no colour from that face.
            mainRay.throughput *= tir ? vec3(1.0) : filterColour * (1.0 - fresnel);
            if (!tir && !hidden) {
                raySetInGlass(mainRay, !inside);
                if (!inside) { raySetGlassParentWater(mainRay, water); }
                raySetInWater(mainRay, inside && parentWater);
                raySetCrossedGlass(mainRay, true);
            }
            // A view off or through another surface stays one past the glass: the ray generation shader reads
            // the lobe a ray leaves by. (The reflection on this face is the camera's own view only.)
            if (!cameraView) { raySetLobeType(mainRay, tir ? 1u : 2u); }
            vec3 outgoing = normalize(tir ? reflected : transmitted);
            mainRay.hitT = g_hit.hitT;
            mainRay.coneWidth += mainRay.hitT * mainRay.coneSpread;
            vec3 hit = g_hit.rayOrigin + incident * g_hit.hitT;
            mainRay.origin = hit + outgoing * 0.0002;
            mainRay.direction = outgoing;
            ptRequestReflection(!tir && !hidden ? reflected : vec3(0.0), reflectionWeight,
                                (water ? 1u : 0u) + (inside ? 2u : 0u) + (parentWater ? 4u : 0u));
            mainRay.hasPrevScenePos = 0u;
            ptStoreMaterial(vec4(1.0), vec3(0.04), 1.0, 0.0, 1.0, 1.0, 0.0);
            // The same medium bits through the auxiliary pair, where the raygen reads them today. Three small
            // integers fit exactly in a half float.
            ptStoreAux(vec2(0.0, (water ? 1.0 : 0.0) + (inside ? 2.0 : 0.0) + (parentWater ? 4.0 : 0.0)));
            raySetNoisy(mainRay, false);
            raySetContinue(mainRay, true);
            raySetPassThrough(mainRay, true);
            raySetStop(mainRay, false);
            return true;
        }
    }
    return false;
}

// How bright a raindrop streak is: the share of the light around it that it sends to the eye at full coverage.
#ifndef VPT_RAIN_STREAK_BRIGHTNESS
#    define VPT_RAIN_STREAK_BRIGHTNESS 1.0
#endif
// The streak's light at Rain Streaks 100 %: the share of the overcast sky over it that a falling drop gives back.
// There is no floor: a fixed light, however faint, is what the eye opened up for a dark night shows as bright white
// streaks; at night rain shows where it dims the lit ground behind it (Rain Opacity) and by the night sky's glint.
const float RAIN_STREAK_SKY = 0.35;

// A raindrop streak met by the camera's view: seen through, with a faint glint of the light around it added on top,
// as glass is (ptGlassPassThrough). It was a surface of its own before, refracted and lit like a sheet of water, and
// the denoiser and the upscaler took the streak, flickering in and out from frame to frame (the any-hit keeps a
// streak at random by its alpha), for what the pixel showed: its normal, depth and falling motion stood in for the
// ground behind it, which smeared and sparkled through every storm, and each streak paid for sun and lamp shadow
// rays. The glint is worked out from the sky over it, with no rays. Bounced rays never meet rain (prepareSecondaryBase).
// How far a splash's drop bends the view through it, per unit of its alpha's slope across a texel.
#ifndef VPT_SPLASH_REFRACTION
#    define VPT_SPLASH_REFRACTION 0.3
#endif
// The share of the sky over it a drop sends to the eye: a sphere of water's reflectance, about 6 %. More, and on dark
// ground every drop showed as a pale speck.
const float SPLASH_GLINT = 0.06;

// A drop of a splash of water (the particles rain throws up where it lands, a fish's, a boat's), marked by the
// particle collector (radianteIsSplash): a small lens. The view goes on through it bent by the slope of its alpha, read a
// level of detail down so that each drop of the sprite is a dome, and takes a little of the sky's light as a glint, as
// the old raster packs refracted the screen behind a splash. Opaque white specks before; now the ground behind them
// wobbles where they land. The camera's view only: any other ray goes straight through, and shadow rays never stop on
// a drop (world/shadow.rahit).
bool ptSplashPassThrough(uint textureID, vec2 textureUV, vec4 colorLayerValue, vec3 dPduWorld, vec3 dPdvWorld) {
    vec2 texel = 1.0 / vec2(textureSize(textures[nonuniformEXT(textureID)], 0));
    float coverage = clamp(sampleTexture(textures[nonuniformEXT(textureID)], textureUV, 0.0, false).a *
                           colorLayerValue.a, 0.0, 1.0);
    vec3 incident = normalize(g_hit.rayDir);
    vec3 outgoing = incident;
    if (rayBounce(mainRay) == 0u || rayCameraPath(mainRay)) {
        // With Pixel Rain the slope is read at the centre of the texel hit, so each pixel of the drop is a flat facet
        // that bends the view one way: the world behind a splash breaks up into pixels, as the splash itself is drawn.
        vec2 at = VPT_RAIN_PIXELS != 0 ? (floor(textureUV / texel) + 0.5) * texel : textureUV;
        float du = sampleTexture(textures[nonuniformEXT(textureID)], at + vec2(texel.x, 0.0), 1.0, false).a -
                   sampleTexture(textures[nonuniformEXT(textureID)], at - vec2(texel.x, 0.0), 1.0, false).a;
        float dv = sampleTexture(textures[nonuniformEXT(textureID)], at + vec2(0.0, texel.y), 1.0, false).a -
                   sampleTexture(textures[nonuniformEXT(textureID)], at - vec2(0.0, texel.y), 1.0, false).a;
        vec3 tangentU = normalizeF(dPduWorld, vec3(1.0, 0.0, 0.0));
        vec3 tangentV = normalizeF(dPdvWorld, vec3(0.0, 1.0, 0.0));
        // Towards the middle of the drop, as a convex lens draws the view in.
        outgoing = normalize(incident + VPT_SPLASH_REFRACTION * 0.5 * (du * tangentU + dv * tangentV));
    }
    vec3 skyUp = texture(skyFull, vec3(0.0, 1.0, 0.0)).rgb;
    vec3 overcast = vec3(0.1) * smoothstep(-0.3, 0.3, celestialSunDirection().y);
    mainRay.radiance += mainRay.throughput * mix(skyUp, overcast, skyUBO.rainGradient) * SPLASH_GLINT * coverage;
    mainRay.throughput *= mix(vec3(1.0), vec3(0.88, 0.94, 1.0), coverage);
    mainRay.hitT = g_hit.hitT;
    mainRay.coneWidth += mainRay.hitT * mainRay.coneSpread;
    vec3 hit = g_hit.rayOrigin + incident * g_hit.hitT;
    mainRay.origin = hit + outgoing * PASS_THROUGH_STEP;
    mainRay.direction = outgoing;
    ptClearReflectionRequest();
    mainRay.hasPrevScenePos = 0u;
    ptStoreMaterial(vec4(1.0), vec3(0.04), 1.0, 0.0, 1.0, 1.0, 0.0);
    ptStoreAux(vec2(0.0));
    raySetNoisy(mainRay, false);
    raySetContinue(mainRay, true);
    raySetPassThrough(mainRay, true);
    raySetStop(mainRay, false);
    return true;
}

// A redstone torch's halo met by any ray (the any-hit shader keeps every hit on it): its glow, as far as it covers
// (its texel's alpha times the coverage the chunk builder gave it, BlockKinds.TORCH_HALO_ALPHA), added over what is
// seen through it, which takes a little of its red. The same in every frame, where it was red dots kept at random.
bool ptTorchHaloPassThrough(uint textureID, vec2 textureUV, vec4 colorLayerValue, float glow) {
    if (!radianteIsGlowLight(g_hit.packedData) || getAlphaMode(g_hit.packedData) != ALPHA_MODE_STOCHASTIC) {
        return false;
    }
    vec4 texel = sampleTexture(textures[nonuniformEXT(textureID)], textureUV, 0.0, false);
    float coverage = clamp(texel.a * colorLayerValue.a, 0.0, 1.0);
    vec3 colour = pow(max(texel.rgb * colorLayerValue.rgb, vec3(0.0)), vec3(2.2));
    mainRay.radiance += mainRay.throughput * colour * (max(glow, 0.0) * coverage);
    mainRay.throughput *= mix(vec3(1.0), colour, 0.5 * coverage);
    mainRay.hitT = g_hit.hitT;
    mainRay.coneWidth += mainRay.hitT * mainRay.coneSpread;
    mainRay.origin = g_hit.rayOrigin + g_hit.rayDir * (g_hit.hitT + PASS_THROUGH_STEP);
    ptClearReflectionRequest();
    mainRay.hasPrevScenePos = 0u;
    ptStoreMaterial(vec4(1.0), vec3(0.04), 1.0, 0.0, 1.0, 1.0, 0.0);
    ptStoreAux(vec2(0.0));
    raySetNoisy(mainRay, false);
    raySetContinue(mainRay, true);
    raySetPassThrough(mainRay, true);
    raySetStop(mainRay, false);
    return true;
}

bool ptRainPassThrough(uint textureID, vec2 textureUV, vec4 colorLayerValue, vec3 dPduWorld, vec3 dPdvWorld) {
    if (radianteIsSplash(g_hit.packedData)) {
        return ptSplashPassThrough(textureID, textureUV, colorLayerValue, dPduWorld, dPdvWorld);
    }
    if (!radianteIsRain(g_hit.packedData)) { return false; }
    vec4 texel = sampleTexture(textures[nonuniformEXT(textureID)], textureUV, 0.0, false);
    float coverage = clamp(texel.a * colorLayerValue.a, 0.0, 1.0);
    float opacity = clamp(VPT_RAIN_OPACITY, 0.0, 1.0);
    // Only the sky: the rain's vertices carry full light levels everywhere, so they cannot say where a lamp is.
    vec3 skyUp = texture(skyFull, vec3(0.0, 1.0, 0.0)).rgb;
    vec3 overcast = vec3(0.1) * smoothstep(-0.3, 0.3, celestialSunDirection().y);
    vec3 around = mix(skyUp, overcast, skyUBO.rainGradient) * RAIN_STREAK_SKY;
    float glint = coverage * max(VPT_RAIN_STREAK_BRIGHTNESS, 0.0) * (1.0 + 2.0 * opacity);
    mainRay.radiance += mainRay.throughput * around * glint;
    // Rain Opacity: how much of the view a streak takes away, a little blue-grey.
    mainRay.throughput *= mix(vec3(1.0), vec3(0.82, 0.90, 1.0), coverage * opacity);
    // The streak falls past a still background: the pixel's motion is the ground's, so DLSS Ray Reconstruction
    // carried the streak over its history and, with the camera still, drew the rain as long straight lines. The
    // bias mask tells it to take this frame's colour here (main_paths.glsl).
    if (coverage > 0.02 && (rayBounce(mainRay) == 0u || rayCameraPath(mainRay))) { raySetFavourCurrent(mainRay, true); }
    mainRay.hitT = g_hit.hitT;
    mainRay.coneWidth += mainRay.hitT * mainRay.coneSpread;
    mainRay.origin = g_hit.rayOrigin + g_hit.rayDir * (g_hit.hitT + PASS_THROUGH_STEP);
    ptClearReflectionRequest();
    mainRay.hasPrevScenePos = 0u;
    ptStoreMaterial(vec4(1.0), vec3(0.04), 1.0, 0.0, 1.0, 1.0, 0.0);
    ptStoreAux(vec2(0.0));
    raySetNoisy(mainRay, false);
    raySetContinue(mainRay, true);
    raySetPassThrough(mainRay, true);
    raySetStop(mainRay, false);
    return true;
}

#endif // VPT_PT_GLASS_GLSL
