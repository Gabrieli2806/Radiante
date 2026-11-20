#ifndef VPT_PT_MATERIAL_GLSL
#define VPT_PT_MATERIAL_GLSL

// The surface a hit shows: its texture, material maps (LabPBR, a Bedrock pack's own MER and height map), glint,
// overlay, water material and normals, and rain on it, as SampledSurface. Also the per-hit flags the shading reads
// (glass, view bounce, front face, texture level). Moved out of the default closest-hit shader unchanged, so the
// raygen can take the same code later; it expects the bindings and payloads pt_shade_default.glsl declares.

struct SampledSurface {
    vec2 uv;
    float depth;
    bool edgeWall;
    vec3 worldPos;
    vec3 geometricNormal;
    vec3 shadingNormal;
    vec4 albedoValue;
    vec4 specularValue;
    vec4 normalValue;
    vec3 tint;
    LabPBRMat mat;
    // The material came from a Bedrock pack's own MER map (see sampleSurfaceState).
    bool authoredMer;
};

// How much of lava's colour its surface shows once the exposure is applied (linear, before
// the tone curve), and the share of its colour that is left to the light other things throw on it.
// Under 1: at 1.3 the bright swirls of its texture were over white, where the tone mapper keeps the hue and gives
// up the rest, and the whole sheet came out one pale, flat peach (and, with bloom starting at white, in a haze of
// its own). At 0.95 its brightest texels just reach white and the darker ones keep their deep orange.
#ifndef LAVA_DISPLAY
#    define LAVA_DISPLAY 0.95
#endif
#ifndef LAVA_LIT_SHARE
#    define LAVA_LIT_SHARE 0.1
#endif

// The most of a grass blade's or a fern's texels the snow whitens, once the ground round it is white: enough to read
// as snow caught on the plant, and few enough that it still reads as grass.
#ifndef PLANT_SNOW
#    define PLANT_SNOW 0.4
#endif

// Lava as a self-lit sheet; see pt_shade_default.glsl. Radiante PT has the meter that tells the
// exposure here, so the others keep lava's fixed glow.
void ptShapeLava(inout SampledSurface surface) {
    float exposure = 1.0;
    if (lightMeter.bedrockMeter.w == 1.0) {
        float minEV = lightMeter.daylight.w < 0.0 ? -4.0 : -1.0; // As exposure.comp holds it.
        exposure = exp2(clamp(lightMeter.bedrockMeter.x, minEV, 6.0) + log2(clamp(lightMeter.daylight.y, 0.05, 1.0)));
    }
    surface.mat.albedo *= LAVA_LIT_SHARE;
    surface.mat.f0 *= LAVA_LIT_SHARE;
    surface.mat.emission = LAVA_DISPLAY / (max(exposure, 1e-3) * VPT_BEDROCK_SEEN_EMISSION *
                                            max(worldUBO.emissionBrightness, 1e-3));
}

/**
 * A shading normal leaned towards the viewer as far as it takes for the mirror reflection of the view in it to stay
 * out of the face (geometricNormal, facing the viewer): halfway between the viewer and that reflection turned up to
 * just over the face, which keeps the side the normal leans to. A normal steeper than 45 degrees off a face seen
 * straight on reflected into the block itself, and a smooth metal's bevels came out as black lines.
 */
vec3 keepReflectionOutOfFace(vec3 normal, vec3 geometricNormal, vec3 viewDir) {
    vec3 mirrored = reflect(-viewDir, normal);
    float above = dot(mirrored, geometricNormal);
    if (above >= 0.05) { return normal; }
    mirrored = normalizeF(mirrored - geometricNormal * (above - 0.05), geometricNormal);
    return normalizeF(viewDir + mirrored, geometricNormal);
}

vec3 applyNormalMapToBasis(vec3 matNormal,
                           vec3 tangent,
                           vec3 bitangent,
                           vec3 geometricNormal,
                           vec3 viewDir) {
    if (any(isnan(matNormal))) { return geometricNormal; }

    vec3 correctedLocalNormal = matNormal;
    correctedLocalNormal.y = -correctedLocalNormal.y;

    vec3 normal = normalizeF(tangent * correctedLocalNormal.x + bitangent * correctedLocalNormal.y +
                                 geometricNormal * correctedLocalNormal.z,
                             geometricNormal);
    float NdotV = dot(normal, viewDir);
    if (NdotV >= 0.99999) { return normal; }

    vec3 edgeNormal = normal - viewDir * NdotV;
    float edgeNormalLen2 = dot(edgeNormal, edgeNormal);
    if (edgeNormalLen2 <= 1e-10) { return geometricNormal; }

    float weight = 1.0 - NdotV;
    weight = sin(min(weight, PI * 0.5));
    weight = clamp(min(max(NdotV, dot(viewDir, geometricNormal)), 1.0 - weight), 0.0, 1.0);

    float tangentWeight2 = max(1.0 - weight * weight, 0.0);
    if (tangentWeight2 <= 1e-10) { return geometricNormal; }

    return viewDir * weight + edgeNormal * inversesqrt(edgeNormalLen2 / tangentWeight2);
}

/** The hit being shaded is glass or a glass pane (set by main from the vertex flag). */
bool g_hitIsGlass = false;

/**
 * The ray is what the camera sees off or through a surface (RAY_VIEW_BOUNCE_MARK), not light gathered for one. Set
 * by ptShadeDefault from its viewMark parameter: the caller reads the mark as the ray arrives, before the hit writes
 * its own data to the same payload field (ptStoreAux).
 */
bool g_viewBounce = false;
// Whether a Bedrock pack ships this texel of the block atlas (the converter keeps its MER map as authored, and the
// map covers the texel), as opposed to vanilla's texture or a Java pack's. Its alpha is 0 where it covers none.
bool bedrockAuthoredTexel(TextureMapEntry textureMap, vec2 uv) {
    return textureMap.rawMer >= 0 &&
           sampleTexture(textures[nonuniformEXT(textureMap.rawMer)], uv, 0.0, false).a > 0.5 / 255.0;
}

/** The face's normal as its triangles are wound (outward for a block's faces); set by main. */
vec3 g_frontNormal = vec3(0.0, 1.0, 0.0);
/** The texture's level of detail at this hit before it is clamped at the full-size level (see main). */
float g_textureLodUnclamped = 0.0;

// Bedrock RTX's shading of a texture set's height map, in Radiante PT. Every texel of it stays
// flat; only across each edge between two texels, a band reaching heightMapPixelEdgeWidth of a texel into both,
// slopes by how much one stands above the other: the normal there is (h(left) - h(right), h(above) - h(below),
// recipHeightMapDepth), normalised, in the face's frame (along u, along v, out of the face). The slopes fade out as a
// texel shrinks to under half a pixel. A smooth normal map made from the heights (the converter's LabPBR one) slopes
// across every texel instead, and gave each block a rough, hammered look the game does not have.
// The two constants are run-time values of the game; 1/12 and 1/4 are the ones documented for them (the MIT
// licensed mcrtx-shader-template). The heights are the converter's copy of the map, in the raw MER's alpha.
const float BEDROCK_HEIGHT_MAP_EDGE_WIDTH = 1.0 / 12.0;
const float BEDROCK_HEIGHT_MAP_DEPTH_RECIP = 0.25;

float bedrockHeightAt(int map, ivec2 texel, ivec2 origin, int spriteSize) {
    // Within the sprite, wrapping at its edges: a block's texture tiles, and the game's atlas repeats each texture
    // past its edges, so the last texel of one block meets the first of the next.
    ivec2 local = texel - origin;
    local -= spriteSize * ivec2(floor(vec2(local) / float(spriteSize)));
    return (texelFetch(textures[nonuniformEXT(map)], origin + local, 0).a * 255.0 - 1.0) / 253.0;
}

bool bedrockHeightMapNormal(int map,
                            vec2 uv,
                            vec2 atlasUvMin,
                            vec2 atlasUvMax,
                            vec3 dPdu,
                            vec3 dPdv,
                            vec3 viewNormal,
                            vec3 viewDir,
                            out vec3 normal) {
    normal = viewNormal;
    ivec2 size = textureSize(textures[nonuniformEXT(map)], 0);
    if (size.x <= 0 || size.y <= 0) { return false; }
    vec2 p = uv * vec2(size);
    ivec2 texel = clampTexelCoord(ivec2(floor(p)), size);
    // 0: no Bedrock material here; 255: a texture the game shades by a normal map, or by nothing.
    float own = texelFetch(textures[nonuniformEXT(map)], texel, 0).a;
    if (own < 0.5 / 255.0 || own > 254.5 / 255.0) { return false; }

    // The sprite the face's texels lie in: the smallest aligned square of 16 texels or more that holds them all.
    ivec2 lo = ivec2(floor(min(atlasUvMin, atlasUvMax) * vec2(size) + 0.01));
    ivec2 hi = max(ivec2(ceil(max(atlasUvMin, atlasUvMax) * vec2(size) - 0.01)) - 1, lo);
    int spriteSize = 16;
    while (spriteSize < 4096 && (lo.x / spriteSize != hi.x / spriteSize || lo.y / spriteSize != hi.y / spriteSize)) {
        spriteSize *= 2;
    }
    ivec2 origin = (lo / spriteSize) * spriteSize;

    // The nearest edge between texels on each axis, and whether this point lies in the band along it.
    vec2 edge = floor(p + 0.5);
    vec2 offset = p - edge;
    vec2 slope = vec2(0.0);
    if (offset.x >= -BEDROCK_HEIGHT_MAP_EDGE_WIDTH && offset.x < BEDROCK_HEIGHT_MAP_EDGE_WIDTH) {
        int e = int(edge.x);
        slope.x = bedrockHeightAt(map, ivec2(e - 1, texel.y), origin, spriteSize) -
                  bedrockHeightAt(map, ivec2(e, texel.y), origin, spriteSize);
    }
    if (offset.y >= -BEDROCK_HEIGHT_MAP_EDGE_WIDTH && offset.y < BEDROCK_HEIGHT_MAP_EDGE_WIDTH) {
        int e = int(edge.y);
        slope.y = bedrockHeightAt(map, ivec2(texel.x, e - 1), origin, spriteSize) -
                  bedrockHeightAt(map, ivec2(texel.x, e), origin, spriteSize);
    }
    if (slope.x == 0.0 && slope.y == 0.0) { return true; }

    vec3 local = normalize(vec3(slope, BEDROCK_HEIGHT_MAP_DEPTH_RECIP));
    // The game's texture level counts a slanted view as a larger footprint; the slopes are gone at level 2.
    float level = max(g_textureLodUnclamped - log2(max(abs(dot(viewNormal, viewDir)), 1e-3)), 0.0);
    local.xy *= clamp(2.0 - level, 0.0, 1.0);
    local.z = max(sqrt(max(1.0 - dot(local.xy, local.xy), 0.0)), 0.01);

    // The face's frame: along u and along v as the texture runs on it, and out of the face; seen from behind, the
    // whole normal turns over with the face.
    vec3 tangent = normalizeF(dPdu - viewNormal * dot(viewNormal, dPdu), vec3(1.0, 0.0, 0.0));
    vec3 bitangent = dPdv - viewNormal * dot(viewNormal, dPdv);
    bitangent = normalizeF(bitangent - tangent * dot(tangent, bitangent), cross(viewNormal, tangent));
    float side = dot(g_frontNormal, viewNormal) < 0.0 ? -1.0 : 1.0;
    normal = normalizeF(side * (tangent * local.x + bitangent * local.y) + viewNormal * local.z, viewNormal);
    // A band's slope leans the normal up to about 76 degrees off the face. Seen along the face, the bands on the far
    // side of every groove then faced away from the camera and drew a black line each, down the whole of a quartz
    // pillar; seen straight on, a band steeper than 45 degrees reflected into the block itself, and every plate of an
    // iron block was ringed in black (keepReflectionOutOfFace).
    normal = keepReflectionOutOfFace(normal, viewNormal, viewDir);
    float facing = dot(normal, viewDir);
    if (facing < 0.15) { normal = normalizeF(normal + viewDir * (0.15 - facing), viewNormal); }
    return true;
}

#ifndef VPT_PORTAL_TRANSPARENCY
#    define VPT_PORTAL_TRANSPARENCY 0.5
#endif

// How much of a kept raindrop streak is there: 0 is invisible water, 1 the pack's rain texture as an opaque sheet.
#ifndef VPT_RAIN_OPACITY
#    define VPT_RAIN_OPACITY 0.0
#endif

void sampleSurfaceState(bool useTexture,
                        uint textureID,
                        TextureMapEntry textureMap,
                        vec2 atlasUvMin,
                        vec2 atlasUvMax,
                        vec2 uv,
                        float lod,
                        uint alphaMode,
                        vec4 colorLayerValue,
                        vec3 colorLayer,
                        vec3 glint,
                        bool useOverlay,
                        ivec2 overlayUV,
                        vec3 dPduWorld,
                        vec3 dPdvWorld,
                        vec3 baseGeoNormal,
                        bool hasHeightMap,
                        float maxDepthWorld,
                        HeightMapHit localHit,
                        vec3 worldPos,
                        vec3 viewDir,
                        bool isWaterMaterial,
                        bool isFftWaterSurface,
                        out SampledSurface surface) {
    vec4 albedoValue = vec4(1.0);
    vec4 specularValue = vec4(0.0);
    vec4 normalValue = vec4(0.0);
    bool useFlatEdgeBand = false;

    if (useTexture) {
        albedoValue = sampleTexture(textures[nonuniformEXT(textureID)], uv, lod, false);
        // Cut-out coverage at full resolution, as the any-hit shader decided the hit.
        if (alphaMode == ALPHA_MODE_CUTOUT && lod > 0.0) {
            albedoValue.a = sampleTexture(textures[nonuniformEXT(textureID)], uv, 0.0, false).a;
        }
        albedoValue.a = resolveSurfaceAlpha(albedoValue.a * colorLayerValue.a, alphaMode);
        specularValue = textureMap.specular >= 0 ?
                            sampleTexture(textures[nonuniformEXT(textureMap.specular)], uv, lod, false) :
                            vec4(0.0);
        normalValue = textureMap.normal >= 0 ?
                          samplePBRTexture(textures[nonuniformEXT(textureMap.normal)], uv, atlasUvMin, atlasUvMax, lod,
                                           VPT_PBR_SAMPLING_MODE) :
                          vec4(0.0);
        if (hasHeightMap && textureMap.normal >= 0) {
            ivec2 heightMapSize = textureSize(textures[nonuniformEXT(textureMap.normal)], 0);
            // Only where the texture is carved there: the band is for the walls a carved border grows. On a
            // texture with a normal map and no depth it was a ring of flat shading round every face, a dark line
            // round the top of a grass block.
            useFlatEdgeBand = normalValue.a < 0.999 && isEdgeUV(uv, atlasUvMin, atlasUvMax, heightMapSize);
        }
    }

    vec3 texelColour = albedoValue.rgb;
    vec3 tint = vptSurfaceColour(albedoValue.rgb * colorLayer) + glint;
    if (useOverlay) {
        vec4 overlayColor = sampleTexture(textures[nonuniformEXT(worldUBO.overlayTextureID)], overlayUV, 0, false);
        tint = mix(overlayColor.rgb, vptSurfaceColour(albedoValue.rgb * colorLayer), overlayColor.a) + glint;
    }

    albedoValue = vec4(tint, albedoValue.a);
    // Glass and panes read from their texture only how clear they are and their colour: Bedrock packs author
    // glass with subsurface scattering and roughness that made it shade as a milky solid, and a pane's mip levels
    // blend its frame into the clear middle, so from a distance it went grey and let little through.
    if (g_hitIsGlass) {
        float clarity = sampleTexture(textures[nonuniformEXT(textureID)], uv, 0.0, false).a * colorLayerValue.a;
        albedoValue.a = resolveSurfaceAlpha(clarity, alphaMode);
        specularValue = vec4(0.0);
    }
    LabPBRMat mat = convertLabPBRMaterial(albedoValue, specularValue, normalValue);
    bool authoredMer = false;
#if defined(VPT_BEDROCK_MATERIALS) && VPT_BEDROCK_MATERIALS != 0
    // A Bedrock pack's MER map as authored, where the converter kept one. The LabPBR copy of it is averaged down to
    // the colour's size and its metalness quantised, which wiped out the small metallic, smooth specks of ores.
    // Bedrock's meaning of the channels: R metalness, B roughness (GGX alpha = roughness^2, as Radiante's own
    // roughness); alpha only marks the texels the map covers. Emission stays the converted one, which Radiante's
    // light levels are tuned to.
    // Glass too: only its frame reaches here (the rest is seen through, see main), and the frame is the pack's
    // material like any other.
    if (useTexture && textureMap.rawMer >= 0) {
        vec4 mer = samplePBRTexture(textures[nonuniformEXT(textureMap.rawMer)], uv, atlasUvMin, atlasUvMax, lod,
                                    VPT_PBR_SAMPLING_MODE);
        if (mer.a > 0.5 / 255.0) {
            authoredMer = true;
            mat.metallic = mer.r;
            mat.roughness = mer.b;
            mat.albedo = albedoValue.rgb;
            mat.f0 = mix(vec3(0.04), albedoValue.rgb, mer.r);
            // Radiante PT takes the pack's emission too (MER green squared, scaled further on) and
            // Bedrock's fixed index of refraction; the others keep the converted emission their light levels are
            // tuned to.
            mat.ior = 1.5;
            mat.emission = mer.g * mer.g;
        }
    }
#endif
    // A see-through sheet a pack gives a metal's material (the nether portal: Vanilla RTX authors it with a metalness
    // of 0.45 and a roughness of 0.97 over a texture of alpha 0.5 to 0.8). A translucent surface is no metal: read as
    // one it took the metal branch of the conversion, which has no transmission, and was shaded as a rough opaque
    // slab of purple with none of the world behind it showing. It is a clear tinted sheet, with the glow it keeps.
    if (!g_hitIsGlass && alphaMode == ALPHA_MODE_TRANSPARENT && albedoValue.a < 0.95 && mat.metallic > 0.0 &&
        mat.transmission <= 0.0) {
        vec3 sheetHue = albedoValue.rgb / max(max(albedoValue.r, max(albedoValue.g, albedoValue.b)), 1e-3);
        // Portal Transparency: how much of the pack's see-through share is kept (100%), down to an opaque sheet (0%).
        albedoValue.a = 1.0 - clamp(VPT_PORTAL_TRANSPARENCY, 0.0, 1.0) * (1.0 - albedoValue.a);
        mat.metallic = 0.0;
        mat.transmission = 1.0;
        mat.albedo = mix(vec3(1.0), sheetHue, albedoValue.a);
        mat.f0 = vec3(0.04);
        mat.roughness = 0.02;
        mat.ior = 1.5;
    }
    // A texture no Bedrock pack covers (vanilla's, a Java pack's) glows on the scale of its light level: its
    // emission is brought to the scale this one shows a pack's own at (see main and VPT_BEDROCK_SEEN_EMISSION).
    if (!authoredMer) {
#if defined(VPT_EMISSION_MODEL) && VPT_EMISSION_MODEL != 0
        float seen = sqrt(max(mat.emission, 0.0));
#else
        float seen = mat.emission;
#endif
        mat.emission = 14.0 * VPT_DIRECT_LIGHT_STRENGTH * seen / VPT_BEDROCK_SEEN_EMISSION;
    }
    // Beyond Bedrock RTX: a light block looks lit by its own light. Bedrock RTX shows the glow a
    // pack authors, and Vanilla RTX authors little for some (its redstone torch 13 of 255, redstone ore 4), which then
    // lit the room around them without seeming to glow themselves. The converted LabPBR map holds four times the
    // green, raised for such blocks to what their light level shows (EmissionTiles.keepVisibleGlow): read back as a
    // green, it is shown when brighter. Blocks the pack makes glow strongly are left as Bedrock shows them, and the
    // light every block casts stays Bedrock's.
    else {
        int storedEmission = int(round(specularValue.a * 255.0));
        float convertedGreen = storedEmission == 255 ? 0.0 : float(storedEmission) / 1020.0;
        mat.emission = max(mat.emission, convertedGreen * convertedGreen);
    }
    // In the Nether the glow a surface shows is held down by as much as the eye is opened up there (pt_fog.glsl's
    // NETHER_EXPOSURE_SCALE): held low for the sake of the lava, the eye left torchlit rock barely readable, and
    // opened up, lava itself came out a pale peach. Lights cast as before; only what the glowing surface shows. Raised
    // from 0.25 so flames (fire, soul fire, campfires) show brighter; lava's own glow is held down at its source
    // (EmissionTiles.LAVA_MER_GLOW) and the tone mapper keeps the hue of what is over white.
#ifndef NETHER_GLOW_SCALE
#    define NETHER_GLOW_SCALE 0.5
#endif
    if (worldUBO.skyType == 0) { mat.emission *= NETHER_GLOW_SCALE; }

    vec3 geometricNormal = localHit.sideWall ? localHit.geometricNormal : baseGeoNormal;
    vec3 shadingNormal = geometricNormal;
    bool useWaterMaterial = isWaterMaterial && !localHit.sideWall;
    if (useWaterMaterial) {
        // The biome's water colour, as vanilla tints it; the refraction takes its square root, so the surface reads
        // the colour lightly and depth (the pack's water medium, when there is one) does the rest.
        vec3 waterTint = clamp(colorLayer, vec3(0.05), vec3(1.0));
        // Bedrock RTX's own mode keeps the texel's colour, untinted, and its alpha: the faint, pale layer the pack's
        // water texture draws over the surface (see the default closest-hit shader).
        albedoValue.rgb = vptSurfaceColour(texelColour);
        tint = albedoValue.rgb;
        mat.f0 = vec3(0.02);
        mat.albedo = waterTint;
        mat.roughness = 0.01;
        mat.metallic = 0.0;
        mat.transmission = 1.0;
        mat.ior = 1.33;

        if (isFftWaterSurface) {
            vec3 absWorldPos = worldPos + vec3(worldUBO.cameraPos.xyz);
            vec3 waterCoordNormal = baseGeoNormal.y >= 0.0 ? baseGeoNormal : -baseGeoNormal;
            vec2 waterCoord = fftWaterSurfaceCoord(absWorldPos, dPduWorld, dPdvWorld, waterCoordNormal);
#ifdef RADIANTE_HOOK_WATER_WAVES_DEFAULT
            FftWaterSample waterSample = sampleFftWater(waterCoord, worldUBO.gameTime);
#else
            FftWaterSample waterSample = sampleFftWaterShaped(waterCoord, worldUBO.gameTime, fftWaterWaveShape());
#endif
            vec3 tangent, bitangent;
            fftWaterStableBasis(baseGeoNormal, tangent, bitangent);
            vec3 localWaterNormal =
                normalize(vec3(vec2(-waterSample.slope.x, waterSample.slope.y) * VPT_WATER_WAVE_STRENGTH, 1.0));
            mat.roughness = clamp(0.005 + 0.012 * min(length(waterSample.slope), 0.45), 0.005, 0.022);
            shadingNormal = applyNormalMapToBasis(localWaterNormal, tangent, bitangent, baseGeoNormal, viewDir);
        } else if (VPT_WATER_SURFACE_MODE == 2u && bedrockWaterAvailable() && abs(baseGeoNormal.y) > 0.75) {
            float footprint = mainRay.coneWidth + g_hit.hitT * mainRay.coneSpread;
#ifdef RADIANTE_HOOK_WATER_WAVES_DEFAULT
            shadingNormal = bedrockWaterNormal(worldPos + vec3(worldUBO.cameraPos.xyz), baseGeoNormal, viewDir,
                                               footprint, baseGeoNormal.y < 0.0);
#else
            shadingNormal = bedrockWaterNormalShaped(worldPos + vec3(worldUBO.cameraPos.xyz), baseGeoNormal, viewDir,
                                                     footprint, baseGeoNormal.y < 0.0, fftWaterWaveShape());
#endif
        }
        if (abs(baseGeoNormal.y) > 0.75) {
            float rippleFootprint = mainRay.coneWidth + g_hit.hitT * mainRay.coneSpread;
            vec2 interactiveSlope = vptInteractiveRippleSlope(worldPos, 0.0, rippleFootprint);
            if (dot(interactiveSlope, interactiveSlope) > 0.0) {
                vec3 faceUp = dot(baseGeoNormal, viewDir) < 0.0 ? -baseGeoNormal : baseGeoNormal;
                shadingNormal = keepReflectionOutOfFace(
                    vptApplyInteractiveRippleNormal(shadingNormal, baseGeoNormal, interactiveSlope), faceUp, viewDir);
                // A little rougher where the rings run, never smoother: capped at 0.045 it made a pack's rougher water
                // glassy wherever something swam, the hyper-real sheen the rings should not have.
                mat.roughness = mat.roughness + 0.008 * min(length(interactiveSlope), 1.5);
            }
        }
    }

    // Water's sides (and any water face without the wave surface) stay flat, as in Bedrock RTX: the pack's water
    // normal map bent the view through a vertical sheet of water into a blue smear.
    if (!useWaterMaterial && !isFftWaterSurface && VPT_PBR_SAMPLING_MODE != 0u && hasHeightMap && !localHit.sideWall && textureMap.normal >= 0 &&
        !useFlatEdgeBand) {
        geometricNormal = sampleNormal(textures[nonuniformEXT(textureMap.normal)], uv, atlasUvMin, atlasUvMax,
                                       dPduWorld, dPdvWorld, baseGeoNormal, 0, VPT_PBR_SAMPLING_MODE,
                                       maxDepthWorld, viewDir);
    }

    // Glass keeps its face's own normal: the bevel a pack's normal map gives the frame, seen from inside the block
    // through the clear part, faced away from the view and went black along every join.
    bool allowSurfaceNormalMap = !g_hitIsGlass;
    // Transparent glass exits before this surface path. Opaque borders/pane caps
    // keep their authored normal map and the existing surface denoising guides.
    allowSurfaceNormalMap = allowSurfaceNormalMap || (mat.transmission <= EPS && textureMap.normal >= 0);
    if (!isFftWaterSurface && !localHit.sideWall && !useWaterMaterial && allowSurfaceNormalMap) {
        vec3 tangent, bitangent;
        tangent = normalizeF(dPduWorld - geometricNormal * dot(geometricNormal, dPduWorld),
                             abs(geometricNormal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0));
        bitangent = normalizeF(cross(geometricNormal, tangent), dPdvWorld);
        tangent = normalizeF(cross(bitangent, geometricNormal), tangent);
        shadingNormal = applyNormalMapToBasis(mat.normal, tangent, bitangent, geometricNormal, viewDir);
        // A pack's normal map leans as steeply as a height map's bevels (Vanilla RTX Opus's iron block).
        shadingNormal = keepReflectionOutOfFace(shadingNormal,
                                                dot(geometricNormal, viewDir) < 0.0 ? -geometricNormal : geometricNormal,
                                                viewDir);
    }
    // A Bedrock pack's height map, shaded as Bedrock RTX shades one, in place of the smooth normal map the converter
    // made from it.
    if (useTexture && textureMap.rawMer >= 0 && !isFftWaterSurface && !localHit.sideWall && !useWaterMaterial &&
        !g_hitIsGlass) {
        vec3 bevelled;
        if (bedrockHeightMapNormal(textureMap.rawMer, uv, atlasUvMin, atlasUvMax, dPduWorld, dPdvWorld, geometricNormal,
                                   viewDir, bevelled)) {
            shadingNormal = bevelled;
        }
    }

    // A rain streak is falling water, not a grey slab: the stochastic any-hit decides where a streak is (by the
    // texture's alpha and the rain's strength) and a kept one is shaded here as a thin, nearly clear, very smooth
    // sheet of water. It lets the view through (tinted a little blue-grey, by Rain Opacity), takes the sun, the sky
    // and the lamps as a faint glint through Fresnel, glows with nothing of its own, and keeps its face's normal.
    // The pack's own rain texture and material are read only for the shape of the streaks.
    bool isRainStreak = radianteIsRain(g_hit.packedData);
    if (isRainStreak) {
        float rainOpacity = clamp(VPT_RAIN_OPACITY, 0.0, 1.0);
        albedoValue.a = rainOpacity;
        mat.metallic = 0.0;
        mat.transmission = 1.0;
        mat.albedo = mix(vec3(1.0), vec3(0.82, 0.90, 1.0), rainOpacity);
        mat.f0 = vec3(0.02);
        mat.roughness = 0.06;
        mat.ior = 1.02;
        mat.emission = 0.0;
        shadingNormal = geometricNormal;
    }

    // Rain soaks the ground under open sky: darker, glossy, and the bumps of the normal map drown.
    if (!useWaterMaterial && !isFftWaterSurface && !g_hitIsGlass && !isRainStreak) {
        vec3 dryAlbedo = mat.albedo;
        vec2 groundRain = vptGroundRain(worldPos, baseGeoNormal);
        float open = groundRain.x;
        // Water stands on the flat top of a full block, and on a slab's and a stair's half a block up, which took
        // only a little darkening before. Not on a path block or farmland, a sixteenth lower, nor on leaves.
        float topHeight = float(fract(worldUBO.cameraPos.y + double(worldPos.y)));
        bool fullTop = alphaMode == ALPHA_MODE_OPAQUE &&
                       (topHeight < 0.002 || topHeight > 0.998 || abs(topHeight - 0.5) < 0.002);
        // The ground darkens well before water stands on it: the wetness rises over a couple of minutes. Upright
        // faces carry their share already (vptGroundRain); leaves, plants and other thin cut-out things take less. A
        // mob's skin is cut out too, but takes the rain as a block does: it took the leaves' share before, and mobs
        // out in a storm looked dry.
        bool upward = baseGeoNormal.y >= 0.5;
        bool foliage = radianteIsLeaves(g_hit.packedData) || radianteIsPlant(g_hit.packedData) ||
                       radianteIsThin(g_hit.packedData);
        bool solidFace = alphaMode == ALPHA_MODE_OPAQUE || (alphaMode == ALPHA_MODE_CUTOUT && !foliage);
        float faceShare = upward ? (fullTop ? 1.0 : (solidFace ? 0.5 : 0.3)) : (solidFace ? 1.0 : 0.6);
        // The ground's soaking, as far as this top has kept up with it (a block broken off it lately).
        float soak = sqrt(max(min(skyUBO.rainWetness.x, g_vptColumnSoak), 0.0));
        // Grass and earth (tinted by the biome) take the rain into the soil and between the blades: a little darker,
        // never the slick grey of wet stone.
        bool tintedFace = any(lessThan(colorLayer, vec3(0.98)));
        float wetness = groundRain.y * soak * faceShare * (tintedFace ? 0.45 : 1.0);
        if (wetness > 0.0 && VPT_RAIN_PIXELS != 0 && useTexture) {
            // Pixel Rain: wet in the texture's own pixels (vptRainPixelWetness), so a mob's wet skin moves with it.
            // A block's pattern also takes its place in the world, or every block of a kind would be wet alike.
            vec2 texSize = vec2(textureSize(textures[nonuniformEXT(textureID)], 0));
            ivec2 texel = ivec2(floor(uv * texSize));
            float row = (uv.y - atlasUvMin.y) * texSize.y;
            uint seed = 0u;
            if (alphaMode == ALPHA_MODE_OPAQUE) {
                ivec3 cell = ivec3(floor(worldUBO.cameraPos.xyz + dvec3(worldPos - baseGeoNormal * 0.01)));
                seed = uint(cell.x * 73856093 ^ cell.y * 19349663 ^ cell.z * 83492791);
                if (!upward) {
                    // A wall of blocks: the runs of water go down the whole wall from its top, the longest down to
                    // the ground as it goes on raining, instead of each block showing the same short runs.
                    vec3 place = vptWallRainPlace(worldPos, baseGeoNormal);
                    texel = ivec2(int(place.y), int(place.z));
                    row = place.x;
                    seed = uint(int(floor(dot(baseGeoNormal.xz, vec2(1.0, 2.0)) + 3.0)) * 2654435761);
                }
            }
            wetness = vptRainPixelWetness(wetness, texel, row, seed, !upward, soak);
        }
        // A film of standing water over it all while it rains, drying up in patches after (vptPuddle).
        vec3 rainPos = vptRainSurfacePosition(worldPos);
        // Grass and earth: the surfaces the biome tints.
        float porous = any(lessThan(colorLayer, vec3(0.98))) ? 1.0 : 0.0;
        float puddle = open > 0.0 && fullTop ? vptRainPuddleEdge(vptPuddle(rainPos, open, porous)) : 0.0;

        // Rime frost and snow in snowy biomes, melted around heat sources and bright lights. Not on mobs, players,
        // chests or anything else drawn as an entity (they use the overlay texture): the blanket and the band laid
        // by the column under them sat on their heads and backs as if they were blocks.
        // Nor on the top of a dirt path or farmland (a sixteenth lower than a block) unless Snow on Paths & Farmland is
        // on, wherever it is: told by the block's column only, the snow drifting in under a roof lay on them.
        bool pathTop = upward && alphaMode == ALPHA_MODE_OPAQUE && abs(topHeight - 0.9375) < 0.002;
        bool snowFree = radianteIsHeld(g_hit.packedData) || useOverlay || (pathTop && !vptSnowOnPaths());
        g_vptSnowFootprint = mainRay.coneWidth + g_hit.hitT * mainRay.coneSpread;
        // The top edge of the face hit, from its triangle (chunk geometry is only moved, never turned or scaled).
        float faceTopY = max(max(g_hit.p0.pos.y, g_hit.p1.pos.y), g_hit.p2.pos.y);
        float hitY = dot(g_hit.bary, vec3(g_hit.p0.pos.y, g_hit.p1.pos.y, g_hit.p2.pos.y));
        g_vptSnowFaceTop = worldPos.y + (faceTopY - hitY);
        // A side under a roof near its top edge, while snow may lie: is there open air over that edge, or more of the
        // wall (vptGroundSnow)? A short ray along the face just above the edge, towards the block, tells.
        g_vptSnowAboveOpen = false;
        if (!upward && !snowFree && skyUBO.rainWetness.x > 0.0 && abs(baseGeoNormal.y) < 0.1 &&
            g_vptSnowFaceTop - worldPos.y < 0.3 && vptSnowSideUnderCover(worldPos, baseGeoNormal)) {
            vec3 above = vec3(worldPos.x, g_vptSnowFaceTop + 0.2, worldPos.z) + baseGeoNormal * 0.02;
            shadowRay.radiance = vec3(0.0);
            shadowRay.throughput = vec3(1.0);
            shadowRay.insideBoat = 0u;
            shadowRay.pad0 = BLOCK_LIGHT_QUERY;
            traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS, WORLD_MASK, 0, 0, 0, above, 0.0, -baseGeoNormal, 0.05, 1);
            shadowRay.pad0 = 0u;
            g_vptSnowAboveOpen = shadowRay.radiance.x > 0.5;
        }
        vec2 groundSnow = snowFree ? vec2(0.0) : vptGroundSnow(worldPos, baseGeoNormal);
        // The footprints left in the snow (SnowPrints).
        VptSnowPrints prints = upward && skyUBO.snowPrintControl.x > 0.5 && !snowFree
                                   ? vptSnowPrints(rainPos) : VptSnowPrints(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        float frost = 0.0;
        float snowCover = 0.0;
        if (groundSnow.x > 0.0 || groundSnow.y > 0.0) {
            float snowStrength = clamp(VPT_SNOW_FROST_STRENGTH, 0.0, 2.0);
            float frostSoak = sqrt(max(min(skyUBO.rainWetness.x, g_vptSnowSoak), 0.0));
            frost = groundSnow.y * frostSoak * faceShare * snowStrength;
            // Rime settles as a soft film that thickens in broad patches, not texel by texel: the rain's pixel runs
            // and specks, used for it before, sprinkled every wall and floor with white and dark pixels.
            if (frost > 0.0) {
                vec3 rimeAt = vec3(worldUBO.cameraPos.xyz) + worldPos;
                if (VPT_RAIN_PIXELS != 0) { rimeAt = (floor(rimeAt * RAIN_PIXELS_PER_BLOCK) + 0.5) / RAIN_PIXELS_PER_BLOCK; }
                float rimePatch = vptRainValueNoise(rimeAt.xz * 0.45 + rimeAt.y * 0.37 + 19.0);
                frost *= 0.65 + 0.35 * smoothstep(0.2, 0.8, rimePatch);
            }
            // Slabs, stairs and trapdoors take the whole blanket like a full block: they took less, so
            // the snow on them stopped at their edge, thinner than on the ground beside. Leaves and plants take less.
            float snowShare = solidFace ? 1.0 : 0.35;
            if (upward) {
                snowCover = groundSnow.x > 0.0 ? vptSnowCover(rainPos, groundSnow.x * snowShare) : 0.0;
            } else {
                // The band of snow along a side's top edge (vptSnowSideFringe), on the grass overlay of a grass
                // block's side as on the earth under it, the way the snowy grass block draws its sides.
                snowCover = groundSnow.x * min(snowStrength, 1.0) * snowShare;
            }
            // Stepped through to the ground in a footprint, or melted round a heat source held standing still.
            snowCover *= 1.0 - max(0.85 * prints.clear, prints.melt);
            frost *= 1.0 - prints.melt;
        }
        // Grass, ferns and the bushes tinted like them take a dusting of snow on their blades: whole texels of their
        // own texture, more of them towards the tips, at most PLANT_SNOW of them once the ground round them is white.
        // Told by the plant flag and the biome's tint, which flowers, crops, saplings and berry bushes do not have:
        // they keep their colours, as a flower in vanilla's snow does. A snowy field kept its grass a summer green.
        if (!upward && !snowFree && useTexture && radianteIsPlant(g_hit.packedData) && porous > 0.5) {
            float settled = vptPlantSnowSettled(worldPos);
            if (settled > 0.0) {
                vec2 texel = floor(uv * vec2(textureSize(textures[nonuniformEXT(textureID)], 0)));
                float rowInBlock = floor(float(fract(worldUBO.cameraPos.y + double(worldPos.y))) * 16.0) / 16.0;
                float tip = smoothstep(0.1, 0.95, rowInBlock);
                float dusting = settled * tip * PLANT_SNOW * min(clamp(VPT_SNOW_FROST_STRENGTH, 0.0, 2.0), 1.0);
                snowCover = max(snowCover, step(vptRainHash(texel + vec2(53.0, 91.0)), dusting));
            }
        }
#ifndef RADIANTE_HOOK_WEATHER_SURFACE_DEFAULT
        // A pack's own rain and snow (hooks/weather_surface.glsl), before any of it is laid on the material.
        hookWeatherSurface(wetness, puddle, frost, snowCover, worldPos, baseGeoNormal, g_hit.packedData,
                           vec2(open, groundSnow.x));
        wetness = clamp(wetness, 0.0, 1.0);
        puddle = clamp(puddle, 0.0, 1.0);
        frost = clamp(frost, 0.0, 2.0);
        snowCover = clamp(snowCover, 0.0, 1.0);
#endif

        if (wetness > 0.0) {
            vptApplyGroundWetness(mat, wetness, !upward);
            shadingNormal = normalizeF(mix(shadingNormal, geometricNormal, wetness * 0.7), geometricNormal);
        }
        if (puddle > 0.0) {
            vptApplyPuddle(mat, puddle, porous);
            // Flat water, so the mirror image is sharp: the rings are kept faint on it, as their changing slope
            // from frame to frame left the denoiser only a blurred image of the sky to show.
            shadingNormal = vptApplyRainRipples(normalizeF(mix(shadingNormal, geometricNormal, puddle),
                                                           geometricNormal),
                                                vptRainRippleSlope(rainPos), puddle * PUDDLE_RIPPLES);
            float rippleFootprint = mainRay.coneWidth + g_hit.hitT * mainRay.coneSpread;
            vec2 stepRippleSlope = vptInteractiveRippleSlope(rainPos, 1.0, rippleFootprint) * puddle;
            if (dot(stepRippleSlope, stepRippleSlope) > 0.0) {
                vec3 faceUp = dot(baseGeoNormal, viewDir) < 0.0 ? -baseGeoNormal : baseGeoNormal;
                shadingNormal = keepReflectionOutOfFace(
                    vptApplyInteractiveRippleNormal(shadingNormal, baseGeoNormal, stepRippleSlope), faceUp, viewDir);
            }
        }

        if (frost > 0.0 || snowCover > 0.0) {
            vptApplySnowFrost(mat, rainPos, frost, snowCover, upward);
            shadingNormal = normalizeF(mix(shadingNormal, geometricNormal,
                                           clamp(snowCover * 0.80 + frost * 0.25, 0.0, 0.88)),
                                       geometricNormal);
        }
        // Packed snow, slush, and snow or water carried in on the feet (vptApplySnowPrints): the water and the snow
        // smooth the bumps of the surface under them.
        float printCover = max(max(prints.slush, prints.snow), prints.wet * 0.6);
        if (prints.packed > 0.0 || printCover > 0.0) {
            vptApplySnowPrints(mat, prints, dryAlbedo);
            shadingNormal = normalizeF(mix(shadingNormal, geometricNormal, printCover * 0.8), geometricNormal);
        }
#if defined(RADIANTE_DEBUG_VIEW) && RADIANTE_DEBUG_VIEW == 11
        g_radianteDebugRain = max(vec3(wetness, open, puddle), vec3(frost, groundSnow.x, snowCover));
#endif
        // The denoiser's albedo guide follows both rain darkening and snow whitening immediately, so neither lags
        // over several frames when turning a corner.
        // Darkening by its ratio to the dry colour; whitening by the snow's own colour, since on a near black block
        // (obsidian, coal) a ratio of a few hundred was clamped and left the guide black under white snow.
        vec3 albedoRatio = clamp(mat.albedo / max(dryAlbedo, vec3(1e-4)), vec3(0.0), vec3(1.0));
        albedoValue.rgb = clamp(albedoValue.rgb * pow(albedoRatio, vec3(1.0 / 2.2)), vec3(0.0), vec3(1.0));
        if (frost > 0.0 || snowCover > 0.0 || prints.snow > 0.0) {
            albedoValue.rgb = max(albedoValue.rgb, pow(clamp(mat.albedo, vec3(0.0), vec3(1.0)), vec3(1.0 / 2.2)));
        }
    } else if (useWaterMaterial && baseGeoNormal.y > 0.5 && skyUBO.rainGradient > 0.01) {
        // Rain rings on water under the open sky.
        float open = vptGroundOpen(worldPos, baseGeoNormal);
        if (open > 0.0) {
            vec2 slope = vptRainRippleSlope(vptRainSurfacePosition(worldPos));
            shadingNormal = vptApplyRainRipples(shadingNormal, slope, open);
        }
    }

    surface.uv = uv;
    surface.depth = localHit.depth;
    surface.edgeWall = localHit.edgeWall;
    surface.worldPos = worldPos;
    surface.geometricNormal = geometricNormal;
    surface.shadingNormal = shadingNormal;
    surface.albedoValue = albedoValue;
    surface.specularValue = specularValue;
    surface.normalValue = normalValue;
    surface.tint = tint;
    surface.mat = mat;
    surface.authoredMer = authoredMer;
}

#endif // VPT_PT_MATERIAL_GLSL
