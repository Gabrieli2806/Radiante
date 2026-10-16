#ifndef VPT_PT_DIRECT_GLSL
#define VPT_PT_DIRECT_GLSL

// Light arriving at a surface: the sun or moon (one shadow ray), the block lights (block_light.glsl), the sky seen
// along a direction, a blended face's own light under the Bedrock profile, and last frame's position of the hit
// for motion vectors. Moved out of the default closest-hit shader unchanged; the hit is read through g_hit.

vec3 sampleSurfaceDirectLight(SampledSurface surface,
                              vec3 viewDir,
                              vec2 referenceUv,
                              vec3 referenceWorldPos,
                              vec2 atlasUvMin,
                              vec2 atlasUvMax,
                              vec3 dPduWorld,
                              vec3 dPdvWorld,
                              vec3 baseGeoNormal,
                              bool traceLocalHeight,
                              int normalTextureID,
                              float maxDepthWorld,
                              bool isFftWaterSurface) {
    // The overworld's sun or moon, or the End's flash while it lasts (shadow.rmiss gives its light).
    bool endFlash = worldUBO.skyType == 2 && endFlashIntensity() > 1e-3;
    if (worldUBO.skyType != 1 && !endFlash) { return vec3(0.0); }

    bool isOpaqueSurface = surface.mat.transmission <= EPS;
    vec3 lightDir = celestialSunDirection();
    if (lightDir.y < 0.0 && !endFlash) { lightDir = -lightDir; }

    vec3 sampledLightDir = SampleVMF(mainRay.seed, lightDir, 3000.0);
    float sampledLightNoL = dot(sampledLightDir, surface.geometricNormal);
    if (isOpaqueSurface && sampledLightNoL <= 0.0) { return vec3(0.0); }

    vec3 lightBRDF;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    if (!endFlash && isOpaqueSurface) {
        // Bedrock evaluates the BRDF at directionToSun. Jitter only the
        // visibility ray: jittering a narrow GGX peak produced bright fireflies
        // on the building and again when the building was reflected in water.
        if (dot(lightDir, surface.geometricNormal) <= 0.0) { return vec3(0.0); }
        lightBRDF = bedrockSolarBrdf(surface.mat, viewDir, surface.shadingNormal, lightDir);
    } else
#endif
    {
        float lightPdf;
        lightBRDF = DisneyEval(surface.mat, viewDir, surface.shadingNormal, sampledLightDir, lightPdf);
        if (lightPdf <= 1e-6) { return vec3(0.0); }
    }

    vec3 shadowOriginPos = surface.worldPos;
    vec3 shadowOriginGeomNormal = surface.geometricNormal;
    if (traceLocalHeight) {
        HeightMapHit shadowSelfHit;
        vec2 exitUv;
        float exitDepth;
        float exitDistance;
        if (traceLocalHeightIntersectionAndExit(normalTextureID, atlasUvMin, atlasUvMax, dPduWorld, dPdvWorld,
                                                baseGeoNormal, maxDepthWorld, surface, sampledLightDir,
                                                VPT_PARALLAX_SECONDARY_MAX_STEPS, shadowSelfHit, exitUv, exitDepth,
                                                exitDistance)) {
            return vec3(0.0);
        }
        if (!surface.edgeWall) {
            shadowOriginPos =
                basePlaneWorldPosAtUv(exitUv, referenceUv, referenceWorldPos, dPduWorld, dPdvWorld) -
                baseGeoNormal * exitDepth;
            shadowOriginGeomNormal = baseGeoNormal;
        }
    }

    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = rayInsideBoat(mainRay) ? 1u : 0u;
    shadowRay.pad0 = 0u;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // Incoming medium identifies an underwater receiver, but not which side of a
    // transparent boundary the shadow origin occupies. Restrict this path to opaque surfaces.
    if (rayInWater(mainRay) && isOpaqueSurface) {
        shadowRay.pad0 = UNDERWATER_CELESTIAL_QUERY;
        shadowRay.nearestWaterT = 1000.0;
        shadowRay.waterTransmission = vec3(1.0);
    }
#endif

    vec3 baseWorldPos = shadowOriginPos;
    vec3 exitGeoNormal = shadowOriginGeomNormal;
    vec3 shadowOrigin = baseWorldPos - sampledLightDir * 0.0002;
    if (!surface.edgeWall) {
        vec3 visibilityNormal = dot(sampledLightDir, exitGeoNormal) >= 0.0 ? exitGeoNormal : -exitGeoNormal;
        shadowOrigin = baseWorldPos + visibilityNormal * 0.0002;
    }
    uint shadowMask = WORLD_MASK | PLAYER_MASK;
    if (VPT_CLOUD_MODE != 2u) { shadowMask |= CLOUD_MASK; }
    traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS, shadowMask, 0, 0, 0,
                shadowOrigin, 0.0001, sampledLightDir, 1000.0, 1);

    float progress = skyUBO.rainGradient;
    vec3 lightRadiance = shadowRay.radiance * mainRay.throughput * lightBRDF;
    if (VPT_CLOUD_MODE == 2u) {
        vec3 absoluteShadowOriginPos = baseWorldPos + vec3(worldUBO.cameraPos.xyz);
        float cloudVisibility = volumetricCloudLightVisibility(absoluteShadowOriginPos, lightDir,
                                                               max(VPT_VOLUMETRIC_CLOUD_LIGHT_STEPS, 1), 0.175);
        lightRadiance *= cloudVisibility;
    }
    return mix(lightRadiance, vec3(0.0), progress);
}

#include "common/block_light.glsl"

bool loadPreviousScenePos(uint geometryBufferIndex, uint primitiveID, vec3 baryCoords, out vec3 prevScenePos) {
    uint64_t lastPositionAddr = lastPositionBufferAddrs.addrs[geometryBufferIndex];
    uint64_t lastIndexAddr = lastIndexBufferAddrs.addrs[geometryBufferIndex];
    if (lastPositionAddr == 0 || lastIndexAddr == 0) { return false; }

    IndexBuffer lastIndexBuffer = IndexBuffer(lastIndexAddr);
    uint indexBaseID = 3u * primitiveID;
    uint i0 = lastIndexBuffer.indices[indexBaseID];
    uint i1 = lastIndexBuffer.indices[indexBaseID + 1u];
    uint i2 = lastIndexBuffer.indices[indexBaseID + 2u];

    PositionBuffer lastPositionBuffer = PositionBuffer(lastPositionAddr);
    vec3 p0 = lastPositionBuffer.vertices[i0].pos;
    vec3 p1 = lastPositionBuffer.vertices[i1].pos;
    vec3 p2 = lastPositionBuffer.vertices[i2].pos;
    vec3 prevLocalPos = baryCoords.x * p0 + baryCoords.y * p1 + baryCoords.z * p2;
    mat4 lastModelMat = lastObjToWorldMats.mat[g_hit.instanceId];
    prevScenePos = mat3(lastModelMat) * prevLocalPos + lastModelMat[3].xyz;
    return true;
}

// How much of the open sky is seen from here along a direction: 0 under a roof or behind a wall.
float skyVisibilityAlong(vec3 position, vec3 direction) {
    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = 0u;
    shadowRay.pad0 = BLOCK_LIGHT_QUERY;
    traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS, WORLD_MASK, 0, 0, 0, position, 0.01, direction, 256.0, 1);
    return clamp(dot(shadowRay.radiance, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
}

#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
// The sky's light off a face, for a surface that returns all of it: the sky out from the face, leaning up (a wall's
// half of the sky is the half over the ground), and at a slant around that; none where a ray that way meets a roof.
vec3 bedrockFaceSkyLight(vec3 position, vec3 normal) {
    if (worldUBO.skyType != 1) { return vec3(0.0); }
    vec3 towards = normalize(normal + vec3(0.0, 0.6, 0.0));
    float open = skyVisibilityAlong(position + normal * 0.01, towards);
    if (open <= 0.0) { return vec3(0.0); }
    vec3 side = normalize(abs(towards.y) < 0.9 ? cross(towards, vec3(0.0, 1.0, 0.0)) : vec3(1.0, 0.0, 0.0));
    vec3 other = cross(towards, side);
    vec3 sky = texture(skyFull, towards).rgb * 0.4 +
               (texture(skyFull, normalize(towards + side)).rgb + texture(skyFull, normalize(towards - side)).rgb +
                texture(skyFull, normalize(towards + other)).rgb + texture(skyFull, normalize(towards - other)).rgb) *
                   0.15;
    vec3 rainy = vec3(0.1) * smoothstep(-0.3, 0.3, celestialSunDirection().y);
    // The sky as it is, as the bounced light sees it there (ptEvalMiss in pt/pt_miss.glsl).
    return mix(sky, rainy, skyUBO.rainGradient) * open;
}

// What a texel drawn blended (glass, ice, slime, honey) shows of itself, already multiplied by the path throughput.
// Bedrock RTX lights such a surface as far as it is opaque and sees through the rest: the sun, the lights around and
// the sky on a matt surface of the texel's colour, times its opacity. The light that reaches it by way of other
// surfaces is left out (the camera's ray goes on through, and is not there to gather it).
vec3 bedrockBlendedFaceLight(vec3 position, vec3 normal, vec3 viewDir, vec3 colour, float opacity) {
    SampledSurface face;
    face.uv = vec2(0.0);
    face.depth = 0.0;
    face.edgeWall = false;
    face.worldPos = position;
    face.geometricNormal = normal;
    face.shadingNormal = normal;
    face.albedoValue = vec4(colour, 1.0);
    face.specularValue = vec4(0.0);
    face.normalValue = vec4(0.0);
    face.tint = colour;
    face.authoredMer = false;
    face.mat.albedo = colour * opacity;
    face.mat.f0 = vec3(0.0);
    face.mat.roughness = 1.0;
    face.mat.metallic = 0.0;
    face.mat.subSurface = 0.0;
    face.mat.transmission = 0.0;
    face.mat.ior = 1.5;
    face.mat.emission = 0.0;
    face.mat.normal = vec3(0.0, 0.0, 1.0);
    face.mat.ao = 1.0;
    face.mat.height = 1.0;
    vec3 light = sampleSurfaceDirectLight(face, viewDir, vec2(0.0), position, vec2(0.0), vec2(0.0), vec3(1.0, 0.0, 0.0),
                                          vec3(0.0, 0.0, 1.0), normal, false, -1, 0.0, false);
    light += sampleBlockLight(position, normal, normal, viewDir, face.mat, false);
    light += sampleHeldLight(position, normal, normal, face.mat);
    light += mainRay.throughput * face.mat.albedo * bedrockFaceSkyLight(position, normal);
    return light;
}
#endif

#endif // VPT_PT_DIRECT_GLSL
