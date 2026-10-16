#ifndef VPT_PT_BOUNCE_GLSL
#define VPT_PT_BOUNCE_GLSL

// The bounces after the first hit: the split into reflection and refraction at transparent surfaces and the bounce
// settings. The bounce loop with the SHaRC query and Russian roulette (once traceSecondary) is the PT_STEP_BOUNCE
// half of ptRunSegment (pt_segment.glsl); the hand's averaged samples and the split's two branches are segments that
// world.rgen's single path loop runs one after another.

// rayState is the path as it left the first hit, aux that hit's auxiliary pair (FirstHitPayload.aux): the water
// flag (y > 0.5), in the experimental Bedrock profile whether the view came from inside the water (y > 1.5), and the
// origin bias (x).
bool initTransparent(MainRay rayState,
                     vec2 aux,
                     MaterialInfo mat,
                     vec3 incomingDirection,
                     vec3 normal,
                     vec3 worldPos,
                     uint branchSalt,
                     bool isReflection,
                     out MainRay branchRay) {
    vec3 incident = normalize(incomingDirection);
    vec3 viewDir = -incident;
    float eta = dot(viewDir, normal) > 0.0 ? (1.0 / mat.ior) : mat.ior;
    vec2 branchAux = aux;
    bool isWaterSurface = branchAux.y > 0.5;
    // Bedrock compatibility: a glass block is seen straight through, undistorted, with a Fresnel reflection of the
    // world on its face (as Bedrock RTX shows it); the view behind goes on as a camera view, through the far face
    // as through a pane (pt/pt_shade_default.glsl). Water keeps its refraction.
    bool bedrockGlass = VPT_FRESNEL_MODEL != 0 && !isWaterSurface;
    if (bedrockGlass) { eta = 1.0 / mat.ior; }
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    if (isWaterSurface) {
        eta = branchAux.y > 1.5 ? mat.ior : 1.0 / mat.ior;
        if (dot(incident, normal) > 0.0) { normal = -normal; }
    }
#endif
    float fresnel = clamp(vptInterfaceFresnel(abs(dot(viewDir, normal)), eta), 0.0, 1.0);

    vec3 branchDirection = isReflection ? reflect(incident, normal) :
                           (bedrockGlass ? incident : refract(incident, normal, eta));
    if (dot(branchDirection, branchDirection) <= 1e-6) { return false; }
    branchDirection = normalize(branchDirection);

    // Water is coloured by its medium on the way through (see pt/pt_shade_default.glsl), so its surface lets light
    // through untinted; glass takes its colour here.
    vec3 refractionTint = isWaterSurface && waterMediumActive() ?
                              vec3(1.0) :
                              clamp(viewGlassTint(mat.albedoValue.rgb, mat.albedoValue.a), vec3(0.0), vec3(1.0));
    vec3 branchThroughput = isReflection ? vec3(fresnel) : refractionTint * (1.0 - fresnel);
    if (max(branchThroughput.r, max(branchThroughput.g, branchThroughput.b)) <= 1e-6) { return false; }
    float originBias = isWaterSurface ? max(branchAux.x, 0.0002) : 0.0001;

    branchRay = rayState;
    branchRay.origin = worldPos + (dot(branchDirection, normal) > 0.0 ? normal : -normal) * originBias;
    branchRay.direction = branchDirection;
    branchRay.radiance = vec3(0.0);
    // Seen through stained glass the water is coloured by it like everything else behind the pane. Starting the two
    // branches afresh left ponds and the sea at full brightness in a view the glass had darkened.
    branchRay.throughput = branchThroughput * clamp(g_passThroughTint, vec3(0.0), vec3(1.0));
    branchRay.hitT = 0.0;
    branchRay.seed = xxhash32(uvec3(rayState.seed, branchSalt, worldUBO.seed));
    raySetStop(branchRay, false);
    raySetContinue(branchRay, false);
    raySetNoisy(branchRay, false);
    branchRay.prevScenePos = vec3(0.0);
    branchRay.hasPrevScenePos = 0u;
    branchRay.normal = vec3(0.0);
    raySetLobeType(branchRay, isReflection ? 1u : 2u);
    if (bedrockGlass && !isReflection) { raySetCameraPath(branchRay, true); }
    raySetIgnoreWaterSelf(branchRay, isWaterSurface);
    // The same rule as a water hit's own bounce (see pt/pt_shade_default.glsl): off the top by direction, through a
    // side by swap.
    if (isWaterSurface) {
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
        bool incomingWater = branchAux.y > 1.5;
        raySetInWater(branchRay, isReflection ? incomingWater : !incomingWater);
#else
        if (abs(normal.y) > 0.5) {
            raySetInWater(branchRay, branchDirection.y < 0.0);
        } else if (!isReflection) {
            raySetInWater(branchRay, !rayInWater(rayState));
        }
#endif
    }
    raySetIndirectVolumetricCloud(
        branchRay, vptShouldCaptureIndirectVolumetricCloud(isReflection ? 1u : 2u, mat.roughness));
    branchRay.directLightRadiance = vec3(0.0);
    return true;
}

#ifndef VPT_MIRROR_EXTRA_BOUNCES
#    define VPT_MIRROR_EXTRA_BOUNCES 8
#endif
// A macro rather than a const: the setting changes live, read from the world uniform, and a global const needs a
// value known when the shader is compiled.
#define MIRROR_EXTRA_BOUNCES VPT_MIRROR_EXTRA_BOUNCES

#ifndef VPT_EXPOSURE_DAY_SCALE
#    define VPT_EXPOSURE_DAY_SCALE 0.48
#endif
#ifndef VPT_EXPOSURE_NIGHT_SCALE
#    define VPT_EXPOSURE_NIGHT_SCALE 0.65
#endif
#ifndef VPT_EXPOSURE_DAY_MAX
#    define VPT_EXPOSURE_DAY_MAX 2.5
#endif
#ifndef VPT_BEDROCK_ONE_BOUNCE
#    define VPT_BEDROCK_ONE_BOUNCE 0
#endif
#ifndef VPT_TRANSPARENT_CHECKERBOARD
#    define VPT_TRANSPARENT_CHECKERBOARD 0
#endif

// One of the two cells of a checkerboard that swaps every frame: half the pixels do a piece of work this frame, the
// other half the next, as Bedrock RTX splits its frame. The denoiser fills in the half left out.
bool checkerboardCell(ivec2 pixel) {
    return ((pixel.x + pixel.y + int(worldUBO.frameCounter & 1u)) & 1) == 0;
}

#ifndef VPT_HAND_SAMPLES
#    define VPT_HAND_SAMPLES 3
#endif

// The held item and hand average VPT_HAND_SAMPLES bounce paths from the same first hit (world.rgen,
// PT_PHASE_AVERAGED). They sit right in front of the camera, big on screen, and a map or a block held up shows every
// speck of one-sample noise that the upscaler has not yet smoothed over. The first hit and its direct light are
// shared; only the bounces after it are taken again. The pixels are few, so the extra paths cost little.

#endif // VPT_PT_BOUNCE_GLSL
