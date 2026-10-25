#ifndef VPT_PT_DIRECT_GLSL
#define VPT_PT_DIRECT_GLSL

// Light arriving at a surface: the sun or moon (one shadow ray), the block lights (block_light.glsl), the sky seen
// along a direction, a blended face's own light under the Bedrock profile, and last frame's position of the hit
// for motion vectors. Moved out of the default closest-hit shader unchanged; the hit is read through g_hit.

// Vanilla's moon quad: half-width 20 at distance 100, as the miss shader draws it (evalMoonBillboard).
const float MOON_SPRITE_TAN_HALF = 20.0 / 100.0;
// Points tried on the moon's square before falling back to its centre; a thin crescent covers little of it.
const int MOON_DISC_SAMPLE_TRIES = 6;

// A direction towards a lit texel of the moon's face: uniform over the part of the disc its phase shows, in the
// frame, turn and cut the miss shader draws it with (evalMoonBillboard, celestialBasis). The light came from a point
// at the moon's centre before, and with a crescent it plainly did not come from the moon. found is false when no
// lit texel was met, and the caller keeps its own sample.
vec3 sampleMoonDiscDirection(inout uint seed, vec3 moonDir, out bool found) {
    found = false;
    vec3 right;
    vec3 up;
    if (skyUBO.celestialAxis.w > 0.5) {
        right = normalize(skyUBO.moonAxis.xyz);
        up = normalize(cross(right, -moonDir));
    } else {
        float sign = moonDir.z >= 0.0 ? 1.0 : -1.0;
        float a = -1.0 / (sign + moonDir.z);
        float k = moonDir.x * moonDir.y * a;
        right = normalize(vec3(1.0 + sign * moonDir.x * moonDir.x * a, sign * k, -sign * moonDir.x));
        up = normalize(vec3(k, sign + moonDir.y * moonDir.y * a, -moonDir.y));
    }
    vec4 rect = skyUBO.moonUvRect;
    if (rect.z <= rect.x || rect.w <= rect.y) { return moonDir; }
    ivec2 size = textureSize(textures[nonuniformEXT(skyUBO.moonTextureID)], 0);
    if (size.x <= 0 || size.y <= 0) { return moonDir; }
    vec2 rotation = skyUBO.spriteRotation.zw;
    bool rotated = dot(rotation, rotation) >= 0.5;

    for (int i = 0; i < MOON_DISC_SAMPLE_TRIES; i++) {
        vec2 q = (vec2(rand(seed), rand(seed)) * 2.0 - 1.0) * MOON_SPRITE_TAN_HALF;
        vec2 uv = vec2(1.0) - (q / MOON_SPRITE_TAN_HALF * 0.5 + 0.5);
        vec2 atlasUv = mix(rect.xy, rect.zw, clamp(uv, 0.0, 0.9999));
        vec4 moon = texelFetch(textures[nonuniformEXT(skyUBO.moonTextureID)],
                               clamp(ivec2(atlasUv * vec2(size)), ivec2(0), size - 1), 0);
        // The miss shader's cut: dimmer texels are the faint glow round the disc, not the moon.
        if (max(moon.r, max(moon.g, moon.b)) < 0.25) { continue; }
        // Back from the sprite's turned frame (celestialSpriteRotate) to the sky's.
        vec2 p = rotated ? vec2(rotation.x * q.x + rotation.y * q.y, -rotation.y * q.x + rotation.x * q.y) : q;
        found = true;
        return normalize(moonDir + right * p.x + up * p.y);
    }
    return moonDir;
}

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
    if (lightDir.y < 0.0 && !endFlash) { lightDir = celestialMoonDirection(); }

    vec3 sampledLightDir = SampleVMF(mainRay.seed, lightDir, 3000.0);
    if (!endFlash && celestialSunDirection().y < 0.0) {
        bool onDisc;
        vec3 moonSample = sampleMoonDiscDirection(mainRay.seed, lightDir, onDisc);
        if (onDisc) { sampledLightDir = moonSample; }
    }
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
    uint shadowMask = WORLD_MASK | g_shadowPlayerMask;
    if (VPT_CLOUD_MODE != 2u) { shadowMask |= CLOUD_MASK; }
    float progress = skyUBO.rainGradient;
    if (g_deferShadowRays) {
        // The light is all known but for what the ray lets through (pt_deferred_shadow.glsl).
        vec3 scale = mainRay.throughput * lightBRDF;
        if (VPT_CLOUD_MODE == 2u) {
            vec3 absoluteShadowOriginPos = baseWorldPos + vec3(worldUBO.cameraPos.xyz);
            scale *= volumetricCloudLightVisibility(absoluteShadowOriginPos, lightDir,
                                                    max(VPT_VOLUMETRIC_CLOUD_LIGHT_STEPS, 1), 0.175);
        }
        ptDeferShadowRay(PT_SHADOW_CELESTIAL, shadowOrigin, sampledLightDir, 1000.0, shadowMask, shadowRay.pad0,
                         mix(scale, vec3(0.0), progress));
        return vec3(0.0);
    }
    traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS, shadowMask, 0, 0, 0,
                shadowOrigin, 0.0001, sampledLightDir, 1000.0, 1);

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
#include "util/wind.glsl"

/** Traces the ray left for later in one slot and returns the light it lets through; nothing when none is waiting. */
vec3 ptTraceDeferredShadowRay(uint slot) {
    if (!g_shadowPending[slot]) { return vec3(0.0); }
    g_shadowPending[slot] = false;
    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = g_shadowInsideBoat[slot];
    shadowRay.pad0 = g_shadowQuery[slot];
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    if (g_shadowQuery[slot] == UNDERWATER_CELESTIAL_QUERY) {
        shadowRay.nearestWaterT = 1000.0;
        shadowRay.waterTransmission = vec3(1.0);
    }
#endif
    traceRayEXT(topLevelAS, VPT_SHADOW_RAY_FLAGS, g_shadowMask[slot], 0, 0, 0, g_shadowOrigin[slot], 0.0001,
                g_shadowDirection[slot], g_shadowLength[slot], 1);
    shadowRay.pad0 = 0u;
    // The miss shader gives the sun's or moon's light already through what the ray crossed.
    if (slot == PT_SHADOW_CELESTIAL) { return shadowRay.radiance * g_shadowScale[slot]; }
    // Only whether the sky is in sight, as skyVisibilityAlong answers it.
    if (slot == PT_SHADOW_SKY) {
        return clamp(dot(shadowRay.radiance, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0) * g_shadowScale[slot];
    }
    vec3 visibility = shadowRay.radiance * shadowRay.throughput;
#if VPT_RESTIR != 0
    if (slot == PT_SHADOW_BLOCK && g_shadowRestirStored) {
        g_shadowRestirStored = false;
        if (dot(visibility, vec3(1.0)) <= 0.0) { vptRestirClearWeight(g_shadowRestirHalf); }
    }
#endif
    return visibility * g_shadowScale[slot];
}

/** Traces every ray left for later, in the order they were sampled, and returns the light they let through. */
vec3 ptTraceDeferredShadowRays() {
    // One call per slot rather than a loop: each call's slot is a constant once inlined, so the slots stay in
    // registers instead of an array in memory.
    return ptTraceDeferredShadowRay(PT_SHADOW_CELESTIAL) + ptTraceDeferredShadowRay(PT_SHADOW_BLOCK) +
           ptTraceDeferredShadowRay(PT_SHADOW_HELD) + ptTraceDeferredShadowRay(PT_SHADOW_OFF_HAND);
}

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

/**
 * How far a point of foliage the wind moves (util/wind.glsl) went since the frame before, for its motion vector:
 * the same field the pass that moved it evaluated, now and with last frame's time, strength and camera. hitPos is the
 * point relative to this frame's camera, windCode the code interpolated over its triangle.
 */
vec3 ptWindMotion(vec3 hitPos, float windCode) {
    dvec3 world = worldUBO.cameraPos.xyz + dvec3(hitPos);
    world.xz -= floor(world.xz / double(WIND_PERIOD)) * double(WIND_PERIOD);
    vec3 p = vec3(world);
    vec3 now = windOffset(p, windCode, worldUBO.windTime, worldUBO.windStrength, worldUBO.windWeather) *
               windFade(length(hitPos));
    // The field is read where the point stands at rest, as the pass read it, not where it has been blown to.
    p -= now;
    vec3 restPos = hitPos - now;
    now = windOffset(p, windCode, worldUBO.windTime, worldUBO.windStrength, worldUBO.windWeather) *
          windFade(length(restPos));
    vec3 restFromLastCamera = restPos + vec3(worldUBO.cameraPos.xyz - vptLastWorldUBO.cameraPos.xyz);
    vec3 before = windOffset(p, windCode, vptLastWorldUBO.windTime, vptLastWorldUBO.windStrength,
                             vptLastWorldUBO.windWeather) *
                  windFade(length(restFromLastCamera));
    return now - before;
}

/**
 * Where the light on a first person hand (or what it holds) is worked out. The hand is drawn in front of the eye
 * wherever the eye is, so with the camera against a wall it reaches into the block: every sun, light and bounce ray
 * from a point of it met the block at once, and the hand came out black (Minecraft and Remix draw it lit however
 * close the view is). A point of the hand that lies behind the first world surface along the way from the eye is
 * lit as if it stood just in front of that surface, on the eye's side; a point in the open is lit where it is.
 */
vec3 ptHandLightPos(vec3 eye, vec3 position) {
    vec3 toPosition = position - eye;
    float distanceToPosition = length(toPosition);
    if (distanceToPosition < 1e-4) { return position; }
    vec3 direction = toPosition / distanceToPosition;
    shadowRay.radiance = vec3(1e6);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = 0u;
    shadowRay.pad0 = HAND_CLEARANCE_QUERY;
    traceRayEXT(topLevelAS, gl_RayFlagsNoOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, WORLD_MASK, 0, 0, 0, eye, 0.0,
                direction, distanceToPosition, 1);
    float wallDistance = shadowRay.radiance.x;
    shadowRay.radiance = vec3(0.0);
    shadowRay.pad0 = 0u;
    // Nothing between the eye and the point (a world surface right at the point itself does not count).
    if (wallDistance >= distanceToPosition - 0.002) { return position; }
    return eye + direction * max(wallDistance - 0.05, 0.0);
}

// How much of the open sky is seen from here along a direction: 0 under a roof or behind a wall.
float skyVisibilityAlong(vec3 position, vec3 direction) {
    shadowRay.radiance = vec3(0.0);
    shadowRay.throughput = vec3(1.0);
    shadowRay.insideBoat = 0u;
    shadowRay.pad0 = SKY_VISIBILITY_QUERY;
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
    // The sky as it is, as the bounced light sees it there (world_miss_eval_impl.glsl).
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
