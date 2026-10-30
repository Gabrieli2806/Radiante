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

// How much of lava's colour its surface shows once the exposure is applied (the Bedrock profile; linear, before
// the tone curve), and the share of its colour that is left to the light other things throw on it.
#ifndef LAVA_DISPLAY
#    define LAVA_DISPLAY 1.3
#endif
#ifndef LAVA_LIT_SHARE
#    define LAVA_LIT_SHARE 0.1
#endif

// Lava as a self-lit sheet; see pt_shade_default.glsl. Only the Bedrock profile has the meter that tells the
// exposure here, so the others keep lava's fixed glow.
void ptShapeLava(inout SampledSurface surface) {
#if VPT_BEDROCK_SPECULAR
    float exposure = 1.0;
    if (lightMeter.bedrockMeter.w == 1.0) {
        exposure = exp2(clamp(lightMeter.bedrockMeter.x, -1.0, 6.0) + log2(clamp(lightMeter.daylight.y, 0.05, 1.0)));
    }
    surface.mat.albedo *= LAVA_LIT_SHARE;
    surface.mat.f0 *= LAVA_LIT_SHARE;
    surface.mat.emission = LAVA_DISPLAY / (max(exposure, 1e-3) * VPT_BEDROCK_SEEN_EMISSION *
                                            max(worldUBO.emissionBrightness, 1e-3));
#endif
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

#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
// Bedrock RTX's shading of a texture set's height map, in the experimental Bedrock profile. Every texel of it stays
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
    // pillar. The normal is kept turned at least a little towards the viewer, as the normal maps are.
    float facing = dot(normal, viewDir);
    if (facing < 0.15) { normal = normalizeF(normal + viewDir * (0.15 - facing), viewNormal); }
    return true;
}
#endif

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
    // Not glass, whose clear texels shade as plain smooth glass (above) - but under the experimental Bedrock profile
    // only its frame reaches here (the rest is seen through, see main), and the frame is the pack's material too.
    if (useTexture && textureMap.rawMer >= 0 && (!g_hitIsGlass || VPT_BEDROCK_SPECULAR != 0)) {
        vec4 mer = samplePBRTexture(textures[nonuniformEXT(textureMap.rawMer)], uv, atlasUvMin, atlasUvMax, lod,
                                    VPT_PBR_SAMPLING_MODE);
        if (mer.a > 0.5 / 255.0) {
            authoredMer = true;
            mat.metallic = mer.r;
#if VPT_BEDROCK_SPECULAR
            mat.roughness = mer.b;
#else
            // Never smoother than LabPBR's smoothest: a pack's perfectly smooth texels (the specks of its ice, a
            // beacon's core) gave the lobes no width at all, and showed as black dots.
            mat.roughness = max(mer.b, 0.01);
#endif
            mat.albedo = albedoValue.rgb;
            mat.f0 = mix(vec3(0.04), albedoValue.rgb, mer.r);
#if VPT_BEDROCK_SPECULAR
            // The experimental profile takes the pack's emission too (MER green squared, scaled further on) and
            // Bedrock's fixed index of refraction; the others keep the converted emission their light levels are
            // tuned to.
            mat.ior = 1.5;
            mat.emission = mer.g * mer.g;
#endif
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
#if VPT_BEDROCK_SPECULAR
    // A texture no Bedrock pack covers (vanilla's, a Java pack's) glows as it does under the other profiles: its
    // emission is brought to the scale this one shows a pack's own at (see main and VPT_BEDROCK_SEEN_EMISSION).
    if (!authoredMer) {
#    if defined(VPT_EMISSION_MODEL) && VPT_EMISSION_MODEL != 0
        float seen = sqrt(max(mat.emission, 0.0));
#    else
        float seen = mat.emission;
#    endif
        mat.emission = 14.0 * VPT_DIRECT_LIGHT_STRENGTH * seen / VPT_BEDROCK_SEEN_EMISSION;
    }
#    if defined(VPT_BEDROCK_ENHANCED) && VPT_BEDROCK_ENHANCED != 0
    // The enhanced profile, beyond Bedrock: a light block looks lit by its own light. Bedrock RTX shows the glow a
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
#    endif
#endif
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
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        // Bedrock RTX's own mode keeps the texel's colour, untinted, and its alpha: the faint, pale layer the pack's
        // water texture draws over the surface (see the default closest-hit shader).
        albedoValue.rgb = vptSurfaceColour(texelColour);
        tint = albedoValue.rgb;
#else
        albedoValue.rgb = waterTint;
        tint = waterTint;
#endif
        mat.f0 = vec3(0.02);
        mat.albedo = waterTint;
        mat.roughness = 0.01;
        mat.metallic = 0.0;
        mat.transmission = 1.0;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        mat.ior = 1.33;
#else
        mat.ior = 1.333;
#endif

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
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // Transparent glass exits before this surface path. Opaque borders/pane caps
    // keep their authored normal map and the existing surface denoising guides.
    allowSurfaceNormalMap = allowSurfaceNormalMap || (mat.transmission <= EPS && textureMap.normal >= 0);
#endif
    if (!isFftWaterSurface && !localHit.sideWall && !useWaterMaterial && allowSurfaceNormalMap) {
        vec3 tangent, bitangent;
        tangent = normalizeF(dPduWorld - geometricNormal * dot(geometricNormal, dPduWorld),
                             abs(geometricNormal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0));
        bitangent = normalizeF(cross(geometricNormal, tangent), dPdvWorld);
        tangent = normalizeF(cross(bitangent, geometricNormal), tangent);
        shadingNormal = applyNormalMapToBasis(mat.normal, tangent, bitangent, geometricNormal, viewDir);
    }
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // A Bedrock pack's height map, shaded as Bedrock RTX shades one, in place of the smooth normal map the converter
    // made from it for the other profiles.
    if (useTexture && textureMap.rawMer >= 0 && !isFftWaterSurface && !localHit.sideWall && !useWaterMaterial &&
        !g_hitIsGlass) {
        vec3 bevelled;
        if (bedrockHeightMapNormal(textureMap.rawMer, uv, atlasUvMin, atlasUvMax, dPduWorld, dPdvWorld, geometricNormal,
                                   viewDir, bevelled)) {
            shadingNormal = bevelled;
        }
    }
#endif

    // A rain streak is falling water, not a grey slab: the stochastic any-hit decides where a streak is (by the
    // texture's alpha and the rain's strength) and a kept one is shaded here as a thin, nearly clear, very smooth
    // sheet of water. It lets the view through (tinted a little blue-grey, by Rain Opacity), takes the sun, the sky
    // and the lamps as a faint glint through Fresnel, glows with nothing of its own, and keeps its face's normal.
    // The pack's own rain texture and material are read only for the shape of the streaks.
    bool isRainStreak = isRainSurface(g_hit.packedData);
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
        vec2 groundRain = vptGroundRain(worldPos, baseGeoNormal);
        float open = groundRain.x;
        // Water stands only on the flat top of a full block: a slab, a stair's step, a path block. Elsewhere, and on
        // leaves, the rain only darkens a little: on a roof of stairs and slabs the full film read as sheets of
        // glass broken at every step.
        float topHeight = float(fract(worldUBO.cameraPos.y + double(worldPos.y)));
        bool fullTop = alphaMode == ALPHA_MODE_OPAQUE && (topHeight < 0.002 || topHeight > 0.998);
        // The ground darkens well before water stands on it: the wetness rises over a couple of minutes.
        float wetness = groundRain.y * sqrt(max(skyUBO.rainWetness.x, 0.0)) *
                        (fullTop ? 1.0 : (alphaMode == ALPHA_MODE_OPAQUE ? 0.5 : 0.3));
        if (wetness > 0.0) {
            vptApplyGroundWetness(mat, wetness);
            shadingNormal = normalizeF(mix(shadingNormal, geometricNormal, wetness * 0.7), geometricNormal);
        }
        // A film of standing water over it all while it rains, drying up in patches after (vptPuddle).
        vec3 rainPos = vptRainSurfacePosition(worldPos);
        // Grass and earth: the surfaces the biome tints.
        float porous = any(lessThan(colorLayer, vec3(0.98))) ? 1.0 : 0.0;
        float puddle = open > 0.0 && fullTop ? vptRainPuddleEdge(vptPuddle(rainPos, open, porous)) : 0.0;
        if (puddle > 0.0) {
            vptApplyPuddle(mat, puddle, porous);
            // Flat water, so the mirror image is sharp: the rings are kept faint on it, as their changing slope
            // from frame to frame left the denoiser only a blurred image of the sky to show.
            shadingNormal = vptApplyRainRipples(normalizeF(mix(shadingNormal, geometricNormal, puddle),
                                                           geometricNormal),
                                                vptRainRippleSlope(rainPos), puddle * PUDDLE_RIPPLES);
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
