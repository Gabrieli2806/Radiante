#ifndef VPT_PT_SHADE_DEFAULT_GLSL
#define VPT_PT_SHADE_DEFAULT_GLSL

// The default surface shading: material, water and glass media, parallax, direct light and the next bounce, as the
// closest-hit shaders of the default, text and entity groups run it. default.rchit and no_height.rchit are thin
// wrappers that set PT_PARALLAX_ALLOWED and include this file. The pieces it is built from live in pt_material,
// pt_parallax, pt_direct, pt_media and pt_glass, and every value of the hit (ray, distance, triangle, transforms)
// is read through g_hit (pt_hit_context.glsl), so the same source can later shade in raygen.
// The wrapper supplies #version and the extensions.

// How strongly the pack's water texture shows over the surface (the Water Texture slider): 1 as Bedrock RTX
// draws it, 0 for clear water.
#ifndef VPT_WATER_TEXTURE_LAYER
#    define VPT_WATER_TEXTURE_LAYER 0.1
#endif

#ifndef PT_PARALLAX_ALLOWED
#    error "pt_shade_default.glsl: the including shader must define PT_PARALLAX_ALLOWED (1 or 0)"
#endif

#include "common/shared.hpp"
#include "util/disney.glsl"
#include "common/bedrock_solar_brdf.glsl"
#include "util/alpha_mode.glsl"
#include "common/fft_water.glsl"
#include "util/height_map.glsl"
#include "util/random.glsl"
#include "util/ray_cone.glsl"
#include "util/ray.glsl"
#include "util/sampling_helpers.glsl"
#include "util/util.glsl"

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(set = 1, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout(set = 1, binding = 1) readonly buffer BLASOffsets {
    uint offsets[];
}
blasOffsets;

layout(set = 1, binding = 2) readonly buffer IndexBufferAddr {
    uint64_t addrs[];
}
indexBufferAddrs;

layout(set = 1, binding = 3) readonly buffer LastIndexBufferAddr {
    uint64_t addrs[];
}
lastIndexBufferAddrs;

layout(set = 1, binding = 8) readonly buffer LastObjToWorldMat {
    mat4 mat[];
}
lastObjToWorldMats;

layout(set = 1, binding = 7) readonly buffer TextureMappingBuffer {
    TextureMapping mapping;
};

layout(set = 1, binding = 6) readonly buffer LastPositionBufferAddr {
    uint64_t addrs[];
}
lastPositionBufferAddrs;

layout(set = 2, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 2, binding = 2) uniform SkyUniform {
    SkyUBO skyUBO;
};

// The incident light meter, as pt_bindings.glsl declares it for the ray generation shader: read here for the exposure.
layout(set = 2, binding = 3) readonly buffer LightMeterBuffer {
    vec4 samples[256];
    vec4 daylight;
    vec4 bedrockMeter;
    vec4 sampleHistory[256];
    vec4 sampleSurface[256];
}
lightMeter;

#include "common/celestial.glsl"

layout(set = 5, binding = 0) uniform sampler2D transLUT;
layout(set = 5, binding = 2) uniform samplerCube skyFull;

#include "common/volumetric_cloud.glsl"

layout(std430, buffer_reference, buffer_reference_align = 4) readonly buffer IndexBuffer {
    uint indices[];
}
indexBuffer;

#include "util/vertex.glsl"
#include "common/constants.glsl"
#include "common/water_medium.glsl"
#include "common/rain_wetness.glsl"
#if defined(RADIANTE_DEBUG_VIEW) && RADIANTE_DEBUG_VIEW == 11
// The rain debug view's values for this surface: wetness, open sky, standing water (pt_material.glsl).
vec3 g_radianteDebugRain = vec3(0.0);
#endif
#include "common/seamless_glass.glsl"
// A pack's water waves (hooks/water_waves.glsl); nothing in radiante-pt.
#include "hooks/water_waves.glsl"
#ifndef RADIANTE_HOOK_WATER_WAVES_DEFAULT
#    include "common/fft_water_shaped.glsl"
#endif
#include "common/bedrock_water.glsl"

#ifndef VPT_BOUNCE_LIGHT_BOOST
#    define VPT_BOUNCE_LIGHT_BOOST 1.0
#endif

#ifndef PT_IN_HIT_STAGE
// Until the raygen owns the path state, the payloads below are a closest hit's, so only a hit stage can include this.
#    error "pt_shade_default.glsl: only a closest-hit wrapper (PT_IN_HIT_STAGE) includes this file for now"
#endif
layout(location = 0) rayPayloadInEXT MainRay mainRay;
layout(location = 1) rayPayloadEXT ShadowRay shadowRay;

#include "pt/pt_hit_context.glsl"
#include "pt/pt_sampling.glsl"
#include "pt/pt_side_channels.glsl"
#include "pt/pt_material.glsl"
#include "pt/pt_parallax.glsl"
#include "pt/pt_deferred_shadow.glsl"
// What the surface hook may ask about the texel a hit lies in, and the view it may change: set just before
// hookSurface runs. g_hookViewDir is the direction towards the viewer the surface is shaded with (its reflections and
// highlights); a hook that moves surface.worldPos can point it at the new position. Unused, they compile away.
bool g_hookHasTexel;
vec2 g_hookTexelSize;
vec2 g_hookPlaneUv;
vec3 g_hookPlanePos;
vec3 g_hookDPdu;
vec3 g_hookDPdv;
vec3 g_hookViewDir;

// The centre of the texture's texel at uv, on the face's plane, in camera-relative world space; worldPos when the
// surface has no texture. Follows the texture as it lies on the face: cross plants, vines, rotated or inset quads.
vec3 hookTexelCentre(vec2 uv, vec3 worldPos) {
    if (!g_hookHasTexel) { return worldPos; }
    vec2 centreUv = (floor(uv * g_hookTexelSize) + 0.5) / g_hookTexelSize;
    return basePlaneWorldPosAtUv(centreUv, g_hookPlaneUv, g_hookPlanePos, g_hookDPdu, g_hookDPdv);
}

// The surface flags debug view (common/debug_view.glsl, RADIANTE_DEBUG_VIEW 8, chosen with /radiante debug view
// flags): what kind of surface the camera sees, written here because only the hit shaders read the vertex data. The
// rain view (11) likewise, from what the material worked out. Compiled out unless one of them is chosen.
#ifdef RADIANTE_DEBUG_VIEW
#    if RADIANTE_DEBUG_VIEW == 8 || RADIANTE_DEBUG_VIEW == 11
#        include "common/debug_view.glsl"
layout(set = 5, binding = RADIANTE_DEBUG_VIEW_STORAGE_BINDING, rgba16f) uniform writeonly image2D radianteDebugViewImage;
#    endif
#    if RADIANTE_DEBUG_VIEW == 8
#        define PT_DEBUG_VIEW_FLAGS 1
#    endif
#    if RADIANTE_DEBUG_VIEW == 11
#        define PT_DEBUG_VIEW_RAIN 1
#    endif
#endif

// Hook points a pack extending radiante-pt replaces (hooks/*.glsl); empty here. surface.glsl before direct_light.glsl,
// so the direct light hook can read what the surface hook keeps in its own globals.
#include "hooks/water_surface.glsl"
#include "hooks/surface.glsl"
#include "hooks/direct_light.glsl"
#include "hooks/cloud_shadow.glsl"
#include "pt/pt_direct.glsl"
#include "pt/pt_media.glsl"
#include "pt/pt_glass.glsl"

// Up to how rough a metal is still a mirror looked into (see the mirror path below); LabPBR's smoothest is 0.01.
#ifndef VPT_MIRROR_MAX_ROUGHNESS
#    define VPT_MIRROR_MAX_ROUGHNESS 0.03
#endif
// A macro rather than a const: the setting changes live, read from the world uniform, and a global const needs a
// value known when the shader is compiled.
#define MIRROR_MAX_ROUGHNESS VPT_MIRROR_MAX_ROUGHNESS
const float MIRROR_MIN_METALLIC = 0.5;
// Under how rough a Bedrock pack's full metal is a mirror in the experimental Bedrock profile.
const float BEDROCK_MIRROR_ROUGHNESS = 0.01;

#if VPT_NRD_SIGNALS
// With NRD denoising (VPT_NRD_SIGNALS), the surface the camera sees records the glow it was seen with for the ray
// generation, which hands NRD its signals (world.rgen): the glow is left out of what NRD blurs and added back as it
// is, sharp. (The sun's light was tried the same way, for sharper normal maps: its shadows through leaves and grass
// came out grainy.) One of the ray generation's own output images carries it, at this pixel; the ray generation
// clears it before its camera rays and writes it anew after.
layout(set = 3, binding = 12, rgba16f) uniform image2D nrdRecordEmissionImage; // first_hit_base_emission
#endif

#ifndef RADIANTE_HOOK_RAY_ORIGINS_DEFAULT
// A pack's reflection origin (hooks/ray_origins.glsl): a reflection leaving the point `shadedPos` towards `direction`
// starts hookReflectionOffset() away instead, turned by as much as the view of the moved point reflects off `normal`
// differently from the view of the hit, so every pixel given the same offset reflects the same way (a glossy lobe's
// spread around that direction is kept). Left as it is when the turn would take it to the other side of the surface.
void ptHookMoveReflection(inout vec3 origin, inout vec3 direction, vec3 shadedPos, vec3 normal) {
    vec3 offset = hookReflectionOffset();
    if (!(dot(offset, offset) > 0.0)) { return; }
    vec3 incident = normalize(g_hit.rayDir);
    vec3 movedIncident = normalize(shadedPos + offset - g_hit.rayOrigin);
    vec3 moved = direction + reflect(movedIncident, normal) - reflect(incident, normal);
    if (dot(moved, moved) < 1e-8 || dot(moved, normal) * dot(direction, normal) <= 0.0) { return; }
    origin += offset;
    direction = normalize(moved);
}
#endif

// The old closest-hit main(), unchanged in order, reading the hit through g_hit and handing its results back
// through pt_side_channels.glsl. viewMark: the ray belongs to a view chain (traced with RAY_VIEW_BOUNCE_MARK).
void ptShadeDefault(bool viewMark) {
    uint geometryBufferIndex = g_hit.geometryBufferIndex;
    MaterialVertex m0 = g_hit.m0;
    MaterialVertex m1 = g_hit.m1;
    MaterialVertex m2 = g_hit.m2;
    PositionVertex p0 = g_hit.p0;
    PositionVertex p1 = g_hit.p1;
    PositionVertex p2 = g_hit.p2;

    vec3 baryCoords = g_hit.bary;
    vec3 planeHitWorldPos = g_hit.planeHitWorldPos;
    vec3 baseViewDir = -mainRay.direction;

    uint packedData = g_hit.packedData;
    bool useColorLayer = hasColorLayer(packedData);
    bool useTexture = hasTexture(packedData);
    bool useGlint = hasGlint(packedData);
    bool useOverlay = hasOverlay(packedData);
    vec4 colorLayerValue = useColorLayer ?
                               baryCoords.x * m0.colorLayer + baryCoords.y * m1.colorLayer + baryCoords.z * m2.colorLayer :
                               vec4(1.0);
    vec3 colorLayer = colorLayerValue.rgb;
    uint bounce = rayBounce(mainRay);
    g_viewBounce = viewMark;

    ptWaterMediumAtHit(bounce);

    float albedoEmission =
        baryCoords.x * m0.albedoEmission + baryCoords.y * m1.albedoEmission + baryCoords.z * m2.albedoEmission;
    uint textureID = m0.textureID;
    uint alphaMode = getAlphaMode(packedData);
    uint coordinate = getCoordinate(packedData);

    vec2 textureUV = vec2(0.0);
    TextureMapEntry textureMap = TextureMapEntry(-1, -1, -1, -1);
    vec2 atlasUvMin = vec2(0.0);
    vec2 atlasUvMax = vec2(0.0);
    float lod = 0.0;
    vec3 dposdu = vec3(1.0, 0.0, 0.0);
    vec3 dposdv = vec3(0.0, 1.0, 0.0);
    bool hasHeightMapSurface = false;
    bool hitIsLayer = false;
    float maxDepthWorld = 0.0;
    mat3 objectToWorld = g_hit.objectToWorld;
    mat3 normalMatrix = g_hit.normalMatrix;
    vec3 baseGeoNormal =
        normalizeF(normalMatrix * cross(p1.pos - p0.pos, p2.pos - p0.pos), vec3(0.0, 1.0, 0.0));
    vec3 planeGeoNormal = baseGeoNormal;
    // The face's own normal, as its triangles are wound (outward for a block's faces), before it is turned to the view.
    vec3 windingNormal = baseGeoNormal;
    g_frontNormal = windingNormal;
    vec3 dPduWorld = objectToWorld * dposdu;
    vec3 dPdvWorld = objectToWorld * dposdv;
    bool isWaterMaterial = false;
    bool hasFftWaterSurface = false;
    // Height-map parallax runs only for the groups whose wrapper allows it (no_height.rchit, the entity group that
    // is drawn with a forward depth offset, never takes it), and then only when the pack attribute turns it on.
#if PT_PARALLAX_ALLOWED
    bool parallaxEnabled = VPT_ENABLE_PARALLAX != 0;
#else
    bool parallaxEnabled = false;
#endif
    bool useRealisticWaterSurface = VPT_WATER_SURFACE_MODE == 1u;

    if (useTexture) {
        textureMap = mapping.entries[textureID];
        textureUV = baryCoords.x * m0.textureUV + baryCoords.y * m1.textureUV + baryCoords.z * m2.textureUV;
        atlasUvMin = min(m0.textureUV, min(m1.textureUV, m2.textureUV));
        atlasUvMax = max(m0.textureUV, max(m1.textureUV, m2.textureUV));

        float coneRadiusWorld = mainRay.coneWidth + g_hit.hitT * mainRay.coneSpread;
        computedposduDv(p0.pos, p1.pos, p2.pos, m0.textureUV, m1.textureUV, m2.textureUV, dposdu, dposdv);
        if (isGlassSurface(packedData) && !isHeldSurface(packedData)) {
            textureUV = seamlessGlassUV(textureUV, baryCoords.x * p0.pos + baryCoords.y * p1.pos + baryCoords.z * p2.pos,
                                        dposdu, dposdv, packedData);
            if (seamlessGlassUniform(packedData)) {
                textureUV = seamlessGlassUniformUV(m0.textureUV, m1.textureUV, m2.textureUV);
            }
            // Kept half a texel inside the sprite: on the frame's last column the filtered lookups of the material
            // maps reached into the next sprite of the atlas, and that column came out black in places.
            vec2 halfTexel = 0.5 / vec2(textureSize(textures[nonuniformEXT(textureID)], 0));
            textureUV = clamp(textureUV, atlasUvMin + halfTexel, max(atlasUvMax - halfTexel, atlasUvMin + halfTexel));
        }
        lod = lodWithCone(textures[nonuniformEXT(textureID)], textureUV, coneRadiusWorld, dposdu, dposdv);
        {
            ivec2 lodTexSize = textureSize(textures[nonuniformEXT(textureID)], 0);
            float footprint = max(coneRadiusWorld / max(length(dposdu), 1e-6) * float(lodTexSize.x),
                                  coneRadiusWorld / max(length(dposdv), 1e-6) * float(lodTexSize.y));
            g_textureLodUnclamped = log2(max(footprint, 1e-6));
        }

        dPduWorld = objectToWorld * dposdu;
        dPdvWorld = objectToWorld * dposdv;
        planeGeoNormal = normalizeF(cross(dPduWorld, dPdvWorld), baseGeoNormal);
        baseGeoNormal = planeGeoNormal;
        if (dot(baseGeoNormal, baseViewDir) < 0.0) { baseGeoNormal = -baseGeoNormal; }

        isWaterMaterial = isWaterSurface(packedData);
        g_hitIsGlass = isGlassSurface(packedData);
        hitIsLayer = isLayerSurface(packedData);
        if (textureMap.flag >= 0) {
            ivec4 flags = ivec4(round(sampleTexture(textures[nonuniformEXT(textureMap.flag)], textureUV, ceil(lod), false) * 255.0));
            isWaterMaterial = isWaterMaterial || (flags.r & 0x1) > 0;
        }
        hasFftWaterSurface = useRealisticWaterSurface && isWaterMaterial && abs(planeGeoNormal.y) > 0.75;

        if (ptRainPassThrough(textureID, textureUV, colorLayerValue)) { return; }

#if !defined(VPT_BEDROCK_ORIGINAL) || VPT_BEDROCK_ORIGINAL == 0
        if (ptWaterSidePassThrough(isWaterMaterial, planeGeoNormal, baseGeoNormal, bounce)) { return; }
#endif

#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        if (ptBedrockGlassHit(textureID, textureUV, textureMap, colorLayerValue, colorLayer, windingNormal,
                              baseGeoNormal, bounce)) {
            return;
        }
#else
        if (ptGlassPassThrough(textureID, textureUV, textureMap, colorLayerValue, colorLayer, baseGeoNormal, bounce)) {
            return;
        }
#endif

        if (!isWaterMaterial && parallaxEnabled && bounce == 0u && textureMap.normal >= 0 && coordinate != 1u) {
            maxDepthWorld = heightMapMaxDepthWorld(atlasUvMin, atlasUvMax, dPduWorld, dPdvWorld);
            hasHeightMapSurface =
                maxDepthWorld > heightMapMinWorldDepth && dot(baseViewDir, baseGeoNormal) > VPT_PARALLAX_MIN_VIEW_DOT;
        }
    } else {
        dPduWorld = objectToWorld * dposdu;
        dPdvWorld = objectToWorld * dposdv;
    }
    if (dot(baseGeoNormal, baseViewDir) < 0.0) { baseGeoNormal = -baseGeoNormal; }
    // Glass is flat: carving its frame by a height map put dark side walls along the joins between glass blocks.
    if (g_hitIsGlass) { hasHeightMapSurface = false; }
    // Nor is a layer lifted over a face of its block (the grass block's side overlay): where its carving let the view
    // past an edge, the face under it showed through, and that side of the block lost its overlay.
    if (hitIsLayer) { hasHeightMapSurface = false; }
    vec3 cameraOrigin = vec3(worldUBO.cameraEffectedViewMatInv * vec4(0.0, 0.0, 0.0, 1.0));
    bool traceLocalHeight =
        hasHeightMapSurface && (lod == 0.0 || distance(planeHitWorldPos, cameraOrigin) <= VPT_PARALLAX_CLOSE_DISTANCE) &&
        !hasFftWaterSurface;

    // Where the texture is not carved the ray is on the surface already. Tracing it all the same ran it through the
    // edge handling of carved faces, which along the border of an uncarved one left a dark line - round the top of a
    // grass block with a pack that gives it a normal map and no depth.
    if (traceLocalHeight &&
        sampleHeight(textures[nonuniformEXT(textureMap.normal)], textureUV, atlasUvMin, atlasUvMax, 0, 0u) >= 0.999) {
        traceLocalHeight = false;
    }

    HeightMapHit initialHit = ptParallaxInitialHit(traceLocalHeight, textureMap, textureUV, atlasUvMin, atlasUvMax,
                                                   dPduWorld, dPdvWorld, baseGeoNormal, maxDepthWorld);
    if (ptParallaxEdgePassThrough(initialHit, baseGeoNormal)) { return; }

    vec3 hitWorldPos = planeHitWorldPos + g_hit.rayDir * initialHit.t;
    float actualHitT = g_hit.hitT + initialHit.t;

    mainRay.hitT = actualHitT;
    mainRay.coneWidth += actualHitT * mainRay.coneSpread;
    mainRay.directLightRadiance = vec3(0.0);
    mainRay.hasPrevScenePos = 0u;
    if (bounce == 0u) {
        vec3 prevScenePos;
        if (loadPreviousScenePos(geometryBufferIndex, g_hit.primitiveId, baryCoords, prevScenePos)) {
            // A raindrop seen here was higher up the sheet last frame.
            if (isRainSurface(packedData)) { prevScenePos.y += worldUBO.rainFallPerFrame; }
            mainRay.prevScenePos = prevScenePos;
            mainRay.hasPrevScenePos = 1u;
        } else if (worldUBO.windStrength > 0.0 || vptLastWorldUBO.windStrength > 0.0) {
            // Foliage the wind moves was somewhere else last frame: without its own motion the upscaler's history
            // smeared swaying leaves into ghosts. Only sections carry a wind code, in the w of their positions.
            float windCode = baryCoords.x * uintBitsToFloat(g_hit.p0.pad0) +
                             baryCoords.y * uintBitsToFloat(g_hit.p1.pad0) +
                             baryCoords.z * uintBitsToFloat(g_hit.p2.pad0);
            if (windCode > 0.0) {
                vec3 cameraDelta = vec3(worldUBO.cameraPos.xyz - vptLastWorldUBO.cameraPos.xyz);
                mainRay.prevScenePos = hitWorldPos + cameraDelta - ptWindMotion(hitWorldPos, windCode);
                mainRay.hasPrevScenePos = 1u;
            }
        }
    }

    vec3 glint = vec3(0.0);
    if (useGlint) {
        vec2 glintUV = baryCoords.x * m0.glintUV + baryCoords.y * m1.glintUV + baryCoords.z * m2.glintUV;
        glintUV = (worldUBO.textureMat * vec4(glintUV, 0.0, 1.0)).xy;
        glint = sampleTexture(textures[nonuniformEXT(m0.glintTexture)], glintUV, false).rgb;
    }
    glint *= glint * GLINT_STRENGTH;

    vec3 viewDir = -g_hit.rayDir;

    SampledSurface surface;
    sampleSurfaceState(useTexture, textureID, textureMap, atlasUvMin, atlasUvMax, initialHit.uv, lod, alphaMode,
                       colorLayerValue, colorLayer, glint, useOverlay, m0.overlayUV, dPduWorld, dPdvWorld,
                       baseGeoNormal, hasHeightMapSurface, maxDepthWorld, initialHit, hitWorldPos, viewDir,
                       isWaterMaterial, hasFftWaterSurface, surface);
    // A light block in a state that gives no light draws the sprite its lit state glows with (unlit redstone ore).
    if (isUnlitSurface(packedData)) { surface.mat.emission = 0.0; }
    // Lava is a self-lit sheet: shown by its own colour at a steady brightness. Its glow was a fixed light, and the
    // eye opens and closes with the place (the darker, the further): in a dark cavern or at night the same glow was
    // far over white and lost its texture (a pale peach), by day in the open it was dull. The glow is now what the
    // current exposure brings to LAVA_DISPLAY of its colour, and the light other things throw on it counts for
    // little. (What lava casts on its surroundings is the light list's, unchanged.)
    if (isLavaSurface(packedData) && bounce == 0u) { ptShapeLava(surface); }
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // Small plants (grass, flowers, ferns) are lit as if they faced half way up, as Bedrock RTX lights them: side by
    // side its short grass is three quarters as bright as the grass block under it, where lit by their own upright
    // quads a noon sun only grazed the blades and they came out at two fifths (and facing straight up, brighter than
    // the block).
    if (isPlantSurface(packedData)) {
        vec3 plantNormal = normalize(surface.geometricNormal + vec3(0.0, 0.75, 0.0));
        surface.shadingNormal = plantNormal;
        surface.geometricNormal = plantNormal;
    }
#endif
#ifndef RADIANTE_HOOK_WATER_SURFACE_DEFAULT
    // A pack's water (hooks/water_surface.glsl), with the height of the waves where they bend the surface.
    if (isWaterMaterial) {
        vec3 waterPos = surface.worldPos + vec3(worldUBO.cameraPos.xyz);
        float waveHeight = 0.0;
        if (hasFftWaterSurface) {
            vec3 waterUp = planeGeoNormal.y >= 0.0 ? planeGeoNormal : -planeGeoNormal;
            vec2 waveCoord = fftWaterSurfaceCoord(waterPos, dPduWorld, dPdvWorld, waterUp);
#    ifdef RADIANTE_HOOK_WATER_WAVES_DEFAULT
            waveHeight = sampleFftWater(waveCoord, worldUBO.gameTime).height;
#    else
            waveHeight = sampleFftWaterShaped(waveCoord, worldUBO.gameTime, fftWaterWaveShape()).height;
#    endif
        }
        hookWaterSurface(surface, waterPos, waveHeight);
    }
#endif
    g_hookHasTexel = useTexture;
    g_hookTexelSize = useTexture ? vec2(textureSize(textures[nonuniformEXT(textureID)], 0)) : vec2(1.0);
    g_hookPlaneUv = textureUV;
    g_hookPlanePos = planeHitWorldPos;
    g_hookDPdu = dPduWorld;
    g_hookDPdv = dPdvWorld;
    g_hookViewDir = viewDir;
    hookSurface(surface, packedData, bounce);
    // What the sun lets through it from behind (translucentBackLight), after the pack's hookSurface, which may set
    // surface.mat.subSurface to say how much.
    if (isLeavesSurface(packedData)) {
        ptSetTranslucency(PT_TRANSLUCENCY_LEAVES, surface.mat.subSurface, TRANSLUCENCY_LEAVES);
    } else if (isPlantSurface(packedData)) {
        ptSetTranslucency(PT_TRANSLUCENCY_FOLIAGE, surface.mat.subSurface, TRANSLUCENCY_PLANT);
    } else if (isFrostSurface(packedData)) {
        ptSetTranslucency(PT_TRANSLUCENCY_FROST, surface.mat.subSurface, TRANSLUCENCY_FROST);
    } else {
        ptSetTranslucency(surface.mat.subSurface > 0.0 ? PT_TRANSLUCENCY_FOLIAGE : PT_TRANSLUCENCY_NONE,
                          surface.mat.subSurface, 0.0);
    }
#ifdef PT_DEBUG_VIEW_FLAGS
    if (bounce == 0u) {
        vec3 flagsColor = radianteDebugFlagsColor(isWaterSurface(packedData), isLavaSurface(packedData),
                                                  isGlassSurface(packedData), isPaneSurface(packedData),
                                                  isLeavesSurface(packedData), isPlantSurface(packedData),
                                                  isHeldSurface(packedData), isUnlitSurface(packedData),
                                                  isLayerSurface(packedData), isBlendSurface(packedData));
        imageStore(radianteDebugViewImage, ivec2(gl_LaunchIDEXT.xy), vec4(radianteDebugDisplay(flagsColor), 1.0));
    }
#endif

#ifdef PT_DEBUG_VIEW_RAIN
    if (bounce == 0u) {
        imageStore(radianteDebugViewImage, ivec2(gl_LaunchIDEXT.xy), vec4(g_radianteDebugRain, 1.0));
    }
#endif

#ifndef RADIANTE_HOOK_RAY_ORIGINS_DEFAULT
    // Where the pack's reflections of the surface the camera sees start (hooks/ray_origins.glsl): handed to the ray
    // generation (pt_primary.glsl), which traces the reflection of water and glass, in the material image's fourth
    // layer, as an offset from the camera ray's hit, small enough for half floats wherever the surface is.
    if (bounce == 0u || rayCameraPath(mainRay)) {
        imageStore(rayMaterialStateImage, rayMaterialStateCoord(3),
                   vec4(surface.worldPos + hookReflectionOffset() - hitWorldPos, 0.0));
    }
#endif
    float emissionMultiplier = dot(baryCoords, vec3(m0.emissionMultiplier, m1.emissionMultiplier, m2.emissionMultiplier));
    mainRay.normal = surface.shadingNormal;
    ptStoreMaterial(surface.albedoValue, surface.mat.f0, surface.mat.roughness, surface.mat.metallic,
                    surface.mat.transmission, surface.mat.ior, surface.mat.emission * emissionMultiplier);
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // Save the incoming medium before the sampled bounce changes it. The deterministic split needs it too.
    ptStoreAux(isWaterMaterial ? vec2(VPT_FFT_WATER_ORIGIN_BIAS, rayInWater(mainRay) ? 2.0 : 1.0) : vec2(0.0));
#else
    ptStoreAux(isWaterMaterial ? vec2(VPT_FFT_WATER_ORIGIN_BIAS, 1.0) : vec2(0.0));
#endif

    // A mirror (a smooth metal: SPBR's mirror block, polished metal packs) seen by the camera is looked into, not
    // shaded: the view carries on along the reflection, tinted by the mirror's Fresnel colour, and what it shows
    // becomes the surface the denoiser sees, at its distance behind the mirror. Shaded as a surface, the reflection
    // went through the specular denoiser and came out blurred and noisy.
    // Not what glows: a pack's light with a smooth metallic core (Vanilla RTX's beacon) was looked into there, and
    // its brightest texels, the ones that glow most, showed the room instead of their light.
    // The experimental Bedrock profile: a pack's metal is a mirror only when perfect (metalness 1, roughness under a
    // hundredth), as Bedrock RTX shows it; anything rougher keeps its glossy highlight instead of turning into a flat
    // mirror. Other materials (vanilla's, a Java pack's) keep the rule above.
    bool mirrorSurface = surface.mat.metallic > MIRROR_MIN_METALLIC && surface.mat.roughness <= MIRROR_MAX_ROUGHNESS;
    bool bedrockMirror = false;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    if (surface.authoredMer) {
        mirrorSurface = surface.mat.metallic >= 1.0 - EPS && surface.mat.roughness < BEDROCK_MIRROR_ROUGHNESS;
        bedrockMirror = true;
    }
#endif
    if ((bounce == 0u || rayCameraPath(mainRay)) && !isWaterMaterial && mirrorSurface &&
        surface.mat.transmission <= EPS && surface.mat.emission <= EPS) {
        vec3 incident = normalize(g_hit.rayDir);
        vec3 mirrorNormal = surface.shadingNormal;
        if (dot(mirrorNormal, incident) > 0.0) { mirrorNormal = -mirrorNormal; }
        float cosTheta = clamp(dot(-incident, mirrorNormal), 0.0, 1.0);
        vec3 fresnel = surface.mat.f0 + (vec3(1.0) - surface.mat.f0) * pow(1.0 - cosTheta, 5.0);
        // Bedrock's mirror reflects by its colour alone, at every angle.
        if (bedrockMirror) { fresnel = surface.mat.f0; }
        mainRay.throughput *= clamp(fresnel, vec3(0.0), vec3(1.0));
        mainRay.hitT = actualHitT;
        mainRay.coneWidth += mainRay.hitT * mainRay.coneSpread;
        mainRay.origin = hitWorldPos + mirrorNormal * 0.002;
        mainRay.direction = reflect(incident, mirrorNormal);
#ifndef RADIANTE_HOOK_RAY_ORIGINS_DEFAULT
        ptHookMoveReflection(mainRay.origin, mainRay.direction, surface.worldPos, mirrorNormal);
#endif
        // A pass-through that carries on along the reflection itself: no separate reflection is asked for.
        ptClearReflectionRequest();
        mainRay.hasPrevScenePos = 0u;
        ptStoreMaterial(vec4(1.0), vec3(1.0), 0.0, 1.0, 0.0, 1.0, 0.0);
        raySetNoisy(mainRay, false);
        raySetContinue(mainRay, true);
        raySetPassThrough(mainRay, true);
        raySetStop(mainRay, false);
        return;
    }

    SampledSurface currentSurface = surface;
    vec3 currentViewDir = g_hookViewDir;
    // What the camera's own hand holds up against a wall is lit from the open side of it (see ptHandLightPos).
    bool handShifted = false;
    // The light held in the hand sits inside the held item, where it is: its own ray to the surface starts from the
    // point the surface really is at (a torch pushed into the wall lights its own handle), not from the shifted one.
    vec3 heldLightPos = currentSurface.worldPos;
    if (bounce == 0u && isHeldSurface(packedData) && rayCameraPath(mainRay)) {
        vec3 lightPos = ptHandLightPos(g_hit.rayOrigin, currentSurface.worldPos);
        if (lightPos != currentSurface.worldPos) {
            currentSurface.worldPos = lightPos;
            handShifted = true;
        }
        g_shadowPlayerMask = 0u;
    }
    bool storedLobeType = false;
    for (int localBounce = 0; localBounce < 1; ++localBounce) {
        // A glowing surface is seen brighter than the light it casts (Bedrock keeps the two apart): glowstone and
        // lamps read as glowing whole, as there, while the room they light stays as it was. At 16x (the strength
        // the light is cast with) glowstone showed its bright specks on a near-black block.
        float emissionFactor =
            (bounce == 0u && localBounce == 0) ? 14.0 * VPT_DIRECT_LIGHT_STRENGTH : VPT_INDIRECT_LIGHT_STRENGTH;
        // Seen, a texel glows in proportion to its emissive value, not its square: the dim parts of glowstone
        // between its bright specks glow too instead of going near black. The light cast keeps the square law.
#if VPT_BEDROCK_SPECULAR
        // The experimental Bedrock profile: emission is on Bedrock's own scale there, seen and bounced alike
        // (sampleSurfaceState brings what no pack authored to it).
        emissionFactor = VPT_BEDROCK_SEEN_EMISSION;
#endif
        float seenEmission = currentSurface.mat.emission;
#if defined(VPT_EMISSION_MODEL) && VPT_EMISSION_MODEL != 0 && !VPT_BEDROCK_SPECULAR
        if (bounce == 0u && localBounce == 0) { seenEmission = sqrt(max(seenEmission, 0.0)); }
#endif
        float blockEmissionWeight =
            (localBounce == 0 && bounce > 0u && vptBlockLightAlreadyCounted(g_hit.rayOrigin, currentSurface.worldPos)) ?
                0.0 : 1.0;
        // (The experimental profile desaturates the glow seen as Bedrock does, as much as the light it casts.)
        bool keepGlowColour = bounce == 0u && localBounce == 0 && VPT_BEDROCK_SPECULAR == 0;
#ifdef RADIANTE_HOOK_BLOCK_LIGHT_DEFAULT
        vec3 emissionRadiance =
            blockEmissionWeight * emissionFactor *
            worldUBO.emissionBrightness *
            (keepGlowColour ? currentSurface.tint : vptEmissiveColour(currentSurface.tint)) *
            seenEmission * mainRay.throughput;
#else
        // The glow through a pack's hooks/block_light.glsl, only where there is one: the hook sees the glow's
        // colour, as it sees a light's.
        vec3 glowColour = keepGlowColour ? currentSurface.tint : vptEmissiveColour(currentSurface.tint);
        if (seenEmission > 0.0) { hookBlockLight(glowColour, RADIANTE_BLOCK_LIGHT_GLOW); }
        vec3 emissionRadiance = blockEmissionWeight * emissionFactor * worldUBO.emissionBrightness * glowColour *
                                seenEmission * mainRay.throughput;
#endif
        emissionRadiance *= emissionMultiplier;
        // A glow given as negative (the beacon beam's core) is seen, not cast (transparent_only.rchit).
        float vertexGlow = albedoEmission;
        if (vertexGlow < 0.0) {
            vertexGlow = (bounce == 0u || rayCameraPath(mainRay) || g_viewBounce) ? -vertexGlow : 0.0;
        }
        // A glow that is also in the light list (powered redstone dust) is not counted twice, as glowing texels.
        if (isGlowLightSurface(packedData)) { vertexGlow *= blockEmissionWeight; }
        emissionRadiance += currentSurface.tint * vertexGlow * mainRay.throughput;
        mainRay.radiance += emissionRadiance;
#if VPT_NRD_SIGNALS
        // The surface the camera sees (pass-throughs left out, the paths that fill the cache too). Stored at once:
        // kept to the end, it was one more thing the shadow rays traced there had to keep (pt_deferred_shadow.glsl).
        if ((bounce == 0u || rayCameraPath(mainRay)) && !rayCacheUpdate(mainRay)) {
            imageStore(nrdRecordEmissionImage, ivec2(gl_LaunchIDEXT.xy), vec4(emissionRadiance, 1.0));
        }
#endif

        SampledSurface litSurface = currentSurface;
        // The shadow rays of the lights sampled here are traced as the hit shader ends (main below, see
        // pt_deferred_shadow.glsl); the light they let through is added there.
        g_deferShadowRays = true;
        g_deferredToDirect = localBounce == 0;
        vec3 directLight =
            sampleSurfaceDirectLight(litSurface, currentViewDir, textureUV, planeHitWorldPos, atlasUvMin, atlasUvMax,
                                     dPduWorld, dPdvWorld, baseGeoNormal, traceLocalHeight && !handShifted,
                                     textureMap.normal, maxDepthWorld, hasFftWaterSurface);
        // The surface the camera sees first, straight or through glass: full block light sampling (and ReSTIR).
        bool blockLightPrimary = rayBounce(mainRay) == 0u || rayCameraPath(mainRay);
        // From the second bounce on, block lights are left to be found by the bounces themselves: what they add there is
        // small and averaged away, and sampling them on every bounce cost as much as the rest of the light together.
        // The first bounce samples them (fewer candidates, see sampleBlockLight), with Cache Deep Bounces too: the
        // cache answers from the second bounce on, and a surface lit only by the walls around a lamp (a ceiling around
        // the lights set into it) got nothing of the light those walls take from the lamp, and stayed dark.
        // So does every bounce of the paths that fill the radiance cache: what it keeps for a surface is the light
        // leaving it, lamps included. Reached deeper than the first bounce, a wall went into the cache unlit by the
        // lamp beside it, and every room lit by block lights came out darker the more its light bounced around.
        bool blockLightView = false;
#if VPT_BEDROCK_SPECULAR
        // A reflected/refracted view is not a diffuse deep bounce. Sample its emitters explicitly
        // even after multiple interfaces, without writing the primary pixel's ReSTIR reservoir.
        blockLightView = g_viewBounce;
#endif
        // The experimental profile samples them on every bounce (with the few candidates of the first): its lights
        // are a pack's glowing texels and Bedrock's point lights, and a bounce that has to find a glowing texel by
        // itself finds it rarely and, from far away, at a level of detail where the average of greens squared
        // undercounts it - a room lit by lamps came out a fifth darker on its walls and floor, and the light of a
        // torch, which has no surface to be found, stopped after the second bounce.
        bool blockLightSkipped = !blockLightPrimary && !blockLightView && rayBounce(mainRay) >= 2u &&
                                 !rayCacheUpdate(mainRay) && VPT_BEDROCK_SPECULAR == 0;
        vec3 blockLight = vec3(0.0);
        if (!blockLightSkipped) {
            blockLight = blockLightView && !blockLightPrimary ?
                sampleBlockLight(litSurface.worldPos, litSurface.geometricNormal,
                                 litSurface.shadingNormal, currentViewDir, litSurface.mat, false) :
                sampleBlockLightAt(litSurface.worldPos, litSurface.geometricNormal,
                                   litSurface.shadingNormal, currentViewDir, litSurface.mat, blockLightPrimary);
        }
        directLight += blockLight;
        directLight += sampleHeldLight(handShifted ? heldLightPos : litSurface.worldPos, litSurface.geometricNormal,
                                       litSurface.shadingNormal, litSurface.mat);
        g_deferShadowRays = false;
        if (localBounce == 0) { mainRay.directLightRadiance = directLight; }
        mainRay.radiance += directLight;

        bool isOpaqueSurface = currentSurface.mat.transmission <= EPS;
        vec3 sampleDir;
        float pdf;
        uint lobeType;
        vec3 bsdf;
        if (isWaterMaterial && currentSurface.mat.transmission > EPS) {
            vec3 incident = -currentViewDir;
            vec3 waterNormal = currentSurface.shadingNormal;
            vec3 waterUpNormal = waterNormal.y >= 0.0 ? waterNormal : -waterNormal;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
            float eta = rayInWater(mainRay) ? currentSurface.mat.ior : 1.0 / currentSurface.mat.ior;
            if (dot(incident, waterNormal) > 0.0) { waterNormal = -waterNormal; }
#else
            float eta = dot(incident, waterUpNormal) < 0.0 ? (1.0 / currentSurface.mat.ior) : currentSurface.mat.ior;
#endif
            float fresnel = clamp(vptInterfaceFresnel(abs(dot(currentViewDir, waterNormal)), eta), 0.0, 1.0);
            vec3 refractionDir = refract(incident, waterNormal, eta);
            // Seen from under the water, the surface is let through everywhere with a light cyan tint and little
            // reflection, as in Bedrock RTX: physically only a circle overhead (Snell's window) shows the sky and
            // the rest mirrors the water below, which read as a hole in a turquoise ceiling.
            bool fromBelow = dot(incident, waterUpNormal) > 0.0;
#if !defined(VPT_BEDROCK_ORIGINAL) || VPT_BEDROCK_ORIGINAL == 0
            if (abs(baseGeoNormal.y) <= 0.5) {
                // A side has no up: which way the light crosses it is whether the ray is in the water. Read off the
                // sign of a normal with no vertical part, the medium flipped at random from texel to texel.
                waterNormal = dot(incident, waterNormal) < 0.0 ? waterNormal : -waterNormal;
                eta = rayInWater(mainRay) ? currentSurface.mat.ior : 1.0 / currentSurface.mat.ior;
                fresnel = clamp(vptInterfaceFresnel(abs(dot(currentViewDir, waterNormal)), eta), 0.0, 1.0);
                refractionDir = refract(incident, waterNormal, eta);
                fromBelow = false;
            }
            if (fromBelow) {
                refractionDir = normalize(mix(incident, waterUpNormal, 0.15));
                fresnel = min(fresnel, 0.08);
            }
#endif
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
            // The pack's water texture over the surface, seen from above: as far as a texel is opaque (the upper half
            // of its alpha, Bedrock's rule for every blended face) it is a lit surface of its own colour, untinted,
            // over what is seen through it. Bedrock RTX shows it so: a faint, pale pattern of the texture over every
            // pool and sea, brightest where the texture's alpha peaks, that takes nothing away from the view through
            // the water (1 to 4 blocks of it over white, side by side). Kept in directLightRadiance as well, for the
            // ray generation, which traces the camera's view through the surface itself (initTransparent).
            // Only where the view enters the water from the open air. Water met through glass (an aquarium, a tunnel
            // under the sea) is the side of the water standing against the glass, and with the layer on it every
            // window looked frosted and the tank behind it milky. Nor on a face the view meets from inside the water
            // (the far side of a waterfall, a tank's walls seen through its top): one body of water shows the
            // pattern once, where the view goes into it.
            if (localBounce == 0) {
                mainRay.directLightRadiance = vec3(0.0);
                g_deferredToDirect = false;
            }
            bool waterFromAir = !fromBelow && !rayInWater(mainRay) && !rayCrossedGlass(mainRay);
            if (localBounce == 0 && waterFromAir && (bounce == 0u || rayCameraPath(mainRay) || g_viewBounce)) {
                float waterOpacity = clamp(currentSurface.albedoValue.a * 2.0 - 1.0, 0.0, 1.0) *
                                     clamp(VPT_WATER_TEXTURE_LAYER, 0.0, 1.0);
                if (waterOpacity > 0.0) {
                    vec3 layer = bedrockBlendedFaceLight(currentSurface.worldPos, baseGeoNormal, currentViewDir,
                                                         currentSurface.albedoValue.rgb, waterOpacity) *
                                 (1.0 - fresnel);
                    mainRay.directLightRadiance = layer;
                    mainRay.radiance += layer;
                }
            }
#endif
            bool hasRefraction = dot(refractionDir, refractionDir) > 1e-6;
            bool chooseReflection = !hasRefraction ||
                                    (VPT_TRANSPARENT_SPLIT_MODE != VPT_TRANSPARENT_SPLIT_MODE_DETERMINISTIC &&
                                     rand(mainRay.seed) < fresnel);
            if (chooseReflection) {
                sampleDir = normalize(reflect(incident, waterNormal));
                pdf = max(fresnel, 1e-4);
                lobeType = 1u;
                bsdf = vec3(fresnel);
            } else {
                sampleDir = normalize(refractionDir);
                pdf = max(1.0 - fresnel, 1e-4);
                lobeType = 2u;
                // The water medium colours the water by depth, as Bedrock RTX does (a pack's own, or one made from
                // the biome's water colour); the surface itself is clear.
                vec3 surfaceTint = waterMediumActive() ? vec3(1.0) :
                                                         pow(max(currentSurface.albedoValue.rgb, vec3(0.0)), vec3(0.5));
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
                surfaceTint = vec3(1.0); // Water colour is accumulated over the medium, not a cyan sheet.
#else
                if (fromBelow) { surfaceTint *= vec3(0.85, 0.97, 1.0); }
#endif
                bsdf = surfaceTint * (1.0 - fresnel);
            }
        } else {
            bsdf = DisneySampleXi(currentSurface.mat, currentViewDir, currentSurface.shadingNormal, sampleDir, pdf,
                                  ptBsdfSample(mainRay), lobeType);
        }

        // Leaving the water's surface, through it or off it. Off the top, down goes into the water and up out of it;
        // through a side, the ray swaps sides.
        if (isWaterMaterial) {
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
            if (lobeType == 2u) { raySetInWater(mainRay, !rayInWater(mainRay)); }
#else
            if (abs(baseGeoNormal.y) > 0.5) {
                raySetInWater(mainRay, sampleDir.y < 0.0);
            } else if (lobeType == 2u) {
                raySetInWater(mainRay, !rayInWater(mainRay));
            }
#endif
        }

        raySetBlockLightSampled(mainRay, worldUBO.blockLightSampling != 0u && !blockLightSkipped && lobeType == 0u &&
                                             vptBlockLightDiffuseWeight(currentSurface.mat) > 1e-4);
        raySetBlockLightLean(mainRay, !blockLightPrimary && !blockLightView);

        if (!storedLobeType) {
            raySetLobeType(mainRay, lobeType);
            raySetIndirectVolumetricCloud(mainRay,
                                          vptShouldCaptureIndirectVolumetricCloud(lobeType, currentSurface.mat.roughness));
            storedLobeType = true;
        }
        raySetNoisy(mainRay, true);

        if (pdf <= 1e-6 || max(bsdf.r, max(bsdf.g, bsdf.b)) <= 1e-6) {
            raySetStop(mainRay, true);
            return;
        }

        if (isOpaqueSurface && dot(sampleDir, currentSurface.geometricNormal) <= 0.0) {
            raySetStop(mainRay, true);
            return;
        }

        // The next hit's texture level comes from a cone as wide as the lobe this bounce was drawn from (ray_cone.glsl).
        mainRay.coneSpread = coneSpreadAfterScatter(mainRay.coneSpread, currentSurface.mat.roughness, lobeType);

        vec3 throughputBefore = mainRay.throughput;
        mainRay.throughput *= bsdf / max(pdf, 1e-4);
        // Bedrock RTX fills a room from a single patch of sunlight: light off rough surfaces carries on stronger than
        // their albedo alone lets it (VPT_BOUNCE_LIGHT_BOOST), kept under 0.95 a bounce so it still dies out.
        // (Not in the experimental profile: Bedrock's own light, exposure and tone curve fill the room there.)
        if (lobeType == 0u && VPT_BOUNCE_LIGHT_BOOST > 1.0 && VPT_BEDROCK_SPECULAR == 0) {
            vec3 ratio = mainRay.throughput / max(throughputBefore, vec3(1e-6));
            float bounceAlbedo = max(ratio.r, max(ratio.g, ratio.b));
            mainRay.throughput *= clamp(0.95 / max(bounceAlbedo, 1e-3), 1.0, VPT_BOUNCE_LIGHT_BOOST);
        }

        if (!traceLocalHeight || handShifted) {
            if (hasFftWaterSurface) {
                vec3 exitNormal = dot(sampleDir, currentSurface.geometricNormal) >= 0.0 ? currentSurface.geometricNormal :
                                                                                           -currentSurface.geometricNormal;
                mainRay.origin = currentSurface.worldPos + exitNormal * VPT_FFT_WATER_ORIGIN_BIAS;
            } else if (hasHeightMapSurface && !handShifted) {
                vec3 exitBasePos =
                    basePlaneWorldPosAtUv(currentSurface.uv, textureUV, planeHitWorldPos, dPduWorld, dPdvWorld);
                vec3 exitNormal = dot(sampleDir, baseGeoNormal) >= 0.0 ? baseGeoNormal : -baseGeoNormal;
                mainRay.origin = exitBasePos + exitNormal * 0.0002;
            } else {
                vec3 exitNormal = dot(sampleDir, currentSurface.geometricNormal) >= 0.0 ? currentSurface.geometricNormal :
                                                                                           -currentSurface.geometricNormal;
                mainRay.origin = currentSurface.worldPos + exitNormal * 0.0002;
            }
#ifndef RADIANTE_HOOK_RAY_ORIGINS_DEFAULT
            // The reflection lobe only: diffuse bounces and refraction leave from the shaded point.
            if (lobeType == 1u && !handShifted) {
                ptHookMoveReflection(mainRay.origin, sampleDir, currentSurface.worldPos, currentSurface.shadingNormal);
            }
#endif
            mainRay.direction = sampleDir;
            raySetStop(mainRay, false);
            return;
        }

        HeightMapHit localBounceHit;
        vec2 exitUv;
        float exitDepth;
        float exitDistance;
        bool localBlocked = traceLocalHeightIntersectionAndExit(
            textureMap.normal, atlasUvMin, atlasUvMax, dPduWorld, dPdvWorld, baseGeoNormal, maxDepthWorld,
            currentSurface, sampleDir, VPT_PARALLAX_SECONDARY_MAX_STEPS, localBounceHit, exitUv, exitDepth,
            exitDistance);
        if (!localBlocked) {
            vec3 exitBasePos =
                basePlaneWorldPosAtUv(exitUv, textureUV, planeHitWorldPos, dPduWorld, dPdvWorld) -
                baseGeoNormal * exitDepth;
            vec3 exitGeoNormal =
                localBounceHit.sideWall && localBounceHit.edgeWall ? localBounceHit.geometricNormal : baseGeoNormal;
            vec3 exitNormal = dot(sampleDir, exitGeoNormal) >= 0.0 ? exitGeoNormal : -exitGeoNormal;
            mainRay.origin = exitBasePos + exitNormal * 0.0002;
            mainRay.direction = sampleDir;
            raySetStop(mainRay, false);
            return;
        }

        vec3 nextWorldPos = currentSurface.worldPos + sampleDir * localBounceHit.t;
        sampleSurfaceState(useTexture, textureID, textureMap, atlasUvMin, atlasUvMax, localBounceHit.uv, lod, alphaMode,
                           colorLayerValue, colorLayer, glint, useOverlay, m0.overlayUV, dPduWorld, dPdvWorld,
                           baseGeoNormal, hasHeightMapSurface, maxDepthWorld, localBounceHit, nextWorldPos, -sampleDir,
                           isWaterMaterial, hasFftWaterSurface, currentSurface);
        currentViewDir = -sampleDir;
    }

    raySetStop(mainRay, true);
}

#ifdef PT_IN_HIT_STAGE
void main() {
    ptHitContextFromBuiltins();
    // The view mark is read as the ray arrives: the shading later writes its own data over pad0 (ptStoreAux).
    ptShadeDefault(mainRay.pad0 == RAY_VIEW_BOUNCE_MARK);
    // Last, when the shading keeps nothing else across the traces (see pt_deferred_shadow.glsl).
    vec3 deferredLight = ptTraceDeferredShadowRays();
    mainRay.radiance += deferredLight;
    if (g_deferredToDirect) { mainRay.directLightRadiance += deferredLight; }
    mainRay.radiance += ptTraceDeferredShadowRay(PT_SHADOW_SKY);
}
#endif

#endif // VPT_PT_SHADE_DEFAULT_GLSL
