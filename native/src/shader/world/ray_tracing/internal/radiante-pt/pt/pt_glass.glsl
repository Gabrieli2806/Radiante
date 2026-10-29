#ifndef VPT_PT_GLASS_GLSL
#define VPT_PT_GLASS_GLSL

// Glass seen at a hit: under the Bedrock profile every blended block (glass, ice, slime, honey) is refracted
// through and coloured face by face; otherwise the clear part of glass and panes is seen straight through with a
// Fresnel reflection. Moved out of the default closest-hit shader unchanged; the hit is read through g_hit. Each
// function returns true when it finished the hit as a pass-through.

#ifndef VPT_GLASS_REFLECTION
#    define VPT_GLASS_REFLECTION 1.0
#endif

// The colour one face of see-through glass gives the view through it under the Bedrock profiles. Bedrock RTX filters
// at every face the view crosses, by the colour as light times how clear the texel is (the lower half of the alpha
// range counts as fully clear). A Bedrock pack paints its glass for exactly that: pale, and filtered twice through a
// block it comes out as deep as the glass looks. Glass painted for vanilla's blending (vanilla's own, a Java pack's)
// is far darker, and by that rule a stained window let almost nothing through: it filters by its colour as stored,
// once for the whole block - half at each of the two faces.
vec3 bedrockGlassFaceFilter(vec3 stored, float alpha, bool authored) {
    if (authored) { return clamp(viewGlassTintBedrock(vptSurfaceColour(stored), alpha), vec3(0.0), vec3(1.0)); }
    return sqrt(clamp(viewGlassTintBedrock(stored, alpha), vec3(0.0), vec3(1.0)));
}

#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
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
    if (g_hitIsGlass || isBlendSurface(g_hit.packedData)) {
        vec4 texel = sampleTexture(textures[nonuniformEXT(textureID)], textureUV, 0.0, false);
        float alpha = clamp(texel.a * colorLayerValue.a, 0.0, 1.0);
        bool hidden = !isHeldSurface(g_hit.packedData) && seamlessGlassHidesFace(g_hit.p0.pos, g_hit.p1.pos, g_hit.p2.pos, g_hit.packedData);
        // What the camera sees: straight on, or off and through the surfaces before (a mirror, the water), as
        // Bedrock's view goes on through one surface after another.
        bool cameraView = bounce == 0u || rayCameraPath(mainRay);
        bool viewRay = cameraView || g_viewBounce;
        bool inside = rayInGlass(mainRay);
        // A view goes into glass only through a face turned towards it and comes out only through one turned
        // away: the faces in between (the inner cube of a slime or honey block) are not there for it, nor is the
        // far shell once it is out. Crossed one by one, each bent the view again and coloured it twice over.
        // Not what is held: its faces may be wound either way. (The any-hit shader leaves them out already.)
        if (viewRay && !isHeldSurface(g_hit.packedData) &&
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
#else
bool ptGlassPassThrough(uint textureID,
                        vec2 textureUV,
                        TextureMapEntry textureMap,
                        vec4 colorLayerValue,
                        vec3 colorLayer,
                        vec3 baseGeoNormal,
                        uint bounce) {
    // The clear or tinted part of glass and panes is seen straight through by the camera, as in Bedrock RTX:
    // the view carries on, filtered by the glass's colour, with a faint Fresnel reflection of the sky on top.
    // Refracted as a surface, what lay behind was denoised as if it were on the glass and came out noisy and
    // soft. The frame (opaque texels) stays a surface.
    if (g_hitIsGlass && (bounce == 0u || rayCameraPath(mainRay))) {
        vec4 glassTexel = sampleTexture(textures[nonuniformEXT(textureID)], textureUV, 0.0, false);
        // The texture's own alpha, not the alpha mode's: stained glass is drawn as a stochastic surface, which rounds
        // every texel to fully opaque, and it then never let the view through.
        float glassAlpha = clamp(glassTexel.a * colorLayerValue.a, 0.0, 1.0);
        // The thin top of a pane between two stacked panes (seamless glass) is inside the sheet: seen through as
        // if it were not there. Opaque glass geometry never reaches the any-hit shader that leaves it out.
        bool hiddenPaneTop = !isHeldSurface(g_hit.packedData) && seamlessGlassHidesFace(g_hit.p0.pos, g_hit.p1.pos, g_hit.p2.pos, g_hit.packedData);
        // Tinted glass is nearly opaque in its texture but still a window: seen through, darkened.
        // Only texels that are see-through: the opaque dark pixels along a glass texture's frame, let through as
        // if tinted, came out as black dashes along the joins (an opaque texel's tint lets nothing through).
        if (hiddenPaneTop || glassAlpha < 0.9) {
            vec3 incident = normalize(g_hit.rayDir);
            float cosTheta = clamp(abs(dot(incident, baseGeoNormal)), 0.0, 1.0);
            // Capped: at a grazing angle the full Fresnel term turned the edges of every window into bright rims of
            // sky, glowing at night as if the glass gave off light.
#if VPT_FRESNEL_MODEL != 0
            // Bedrock: the glass face's own Fresnel, air into glass (n 1.5), uncapped.
            float fresnel = vptInterfaceFresnel(cosTheta, 1.0 / 1.5);
#else
            float fresnel = min(0.04 + 0.96 * pow(1.0 - cosTheta, 5.0), 0.25);
#endif
            if (hiddenPaneTop) { fresnel = 0.0; }
            // Only the sky above reflects: what the glass reflects below the horizon is the ground, not the bright
            // haze at the bottom of the sky.
            vec3 reflected = reflect(incident, baseGeoNormal);
            // And the sky well above the horizon at that: a level view reflects level, and the haze along the
            // horizon is the brightest stretch of the whole sky - it lay as a pale band at eye height across
            // every window, over whatever was behind the glass.
            vec3 skyDirection = normalize(vec3(reflected.x, max(reflected.y, 0.45), reflected.z));
            vec3 skyReflection = texture(skyFull, skyDirection).rgb * smoothstep(-0.3, 0.1, reflected.y);
            // Only where that sky is really open: a window inside a room, seen level or from below, reflected
            // the bright sky through the ceiling and glowed as a pale blue sheet.
            if (!hiddenPaneTop && max(skyReflection.r, max(skyReflection.g, skyReflection.b)) > 0.0) {
                vec3 glassPos = g_hit.rayOrigin + incident * g_hit.hitT;
                skyReflection *= skyVisibilityAlong(glassPos - incident * 0.01, skyDirection);
            }
#if VPT_FRESNEL_MODEL != 0
            // Bedrock: the glass reflects the world, not only the sky. The ray generation shader traces that
            // reflection once the view behind is done (ptRequestReflection below, with the reflected direction and
            // this weight).
            vec3 glassReflectionWeight = mainRay.throughput * fresnel * clamp(VPT_GLASS_REFLECTION, 0.0, 1.0);
#else
            mainRay.radiance += mainRay.throughput * fresnel * skyReflection * clamp(VPT_GLASS_REFLECTION, 0.0, 1.0);
#endif
            vec3 filterColour = hiddenPaneTop ? vec3(1.0) : viewGlassTint(glassTexel.rgb * colorLayer, glassAlpha);
            // Dark (tinted) glass shows its own texture lit by the sky, as a dark pane does, not only a dimmed view.
            float tintedness = hiddenPaneTop ? 0.0 : glassDarkness(glassTexel.rgb * colorLayer);
#if VPT_GLASS_TINT_MODEL == 0
            mainRay.radiance += mainRay.throughput * (1.0 - fresnel) * tintedness * 0.35 * glassTexel.rgb *
                                texture(skyFull, vec3(0.0, 1.0, 0.0)).rgb;
#endif
#if VPT_GLASS_TINT_MODEL != 0
            // Bedrock's own rule for a Bedrock pack's glass; any other glass by its colour once a block.
            vec3 faceFilter = hiddenPaneTop ? vec3(1.0) :
                bedrockGlassFaceFilter(glassTexel.rgb * colorLayer, glassAlpha,
                                       bedrockAuthoredTexel(textureMap, textureUV));
            mainRay.throughput *= faceFilter * (1.0 - fresnel);
#else
            // Squared: light crossing a stained pane is coloured as deeply as the pane looks, not washed out.
            mainRay.throughput *= pow(clamp(filterColour, 0.0, 1.0), vec3(2.0)) * (1.0 - fresnel);
#endif
            mainRay.hitT = g_hit.hitT;
            mainRay.coneWidth += mainRay.hitT * mainRay.coneSpread;
            mainRay.origin = g_hit.rayOrigin + incident * (g_hit.hitT + PASS_THROUGH_STEP);
#if VPT_FRESNEL_MODEL != 0
            ptRequestReflection(reflected, glassReflectionWeight, 0u);
#else
            ptClearReflectionRequest();
#endif
            mainRay.hasPrevScenePos = 0u;
            ptStoreMaterial(vec4(1.0), vec3(0.04), 1.0, 0.0, 1.0, 1.0, 0.0);
            raySetNoisy(mainRay, false);
            raySetContinue(mainRay, true);
            raySetPassThrough(mainRay, true);
            raySetStop(mainRay, false);
            return true;
        }
    }
    return false;
}
#endif

// How bright a raindrop streak is: the share of the light around it that it sends to the eye at full coverage.
#ifndef VPT_RAIN_STREAK_BRIGHTNESS
#    define VPT_RAIN_STREAK_BRIGHTNESS 1.0
#endif
// The streak's light at Rain Streaks 100 %: the share of the overcast sky over it that a falling drop gives back.
const float RAIN_STREAK_SKY = 0.05;

// A raindrop streak met by the camera's view: seen through, with a faint glint of the light around it added on top,
// as glass is (ptGlassPassThrough). It was a surface of its own before, refracted and lit like a sheet of water, and
// the denoiser and the upscaler took the streak, flickering in and out from frame to frame (the any-hit keeps a
// streak at random by its alpha), for what the pixel showed: its normal, depth and falling motion stood in for the
// ground behind it, which smeared and sparkled through every storm, and each streak paid for sun and lamp shadow
// rays. The glint is worked out from the sky over it, with no rays. Bounced rays never meet rain (prepareSecondaryBase).
bool ptRainPassThrough(uint textureID, vec2 textureUV, vec4 colorLayerValue) {
    if (!isRainSurface(g_hit.packedData)) { return false; }
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
