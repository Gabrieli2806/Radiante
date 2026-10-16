#ifndef VPT_PT_TRACE_GLSL
#define VPT_PT_TRACE_GLSL

// The one place a main-path ray is traced (payload location 0, the hit group of the geometry: sbtOffset 1, stride 1).
// The shader compiler inlines every function, so each call of a function that traces becomes its own copy of the
// trace and of everything around it, and with it a full copy of the surface shading that runs after the trace
// (ptShadeRecorded). So the pixel's whole path (camera rays, glass reflection, bounces, cache update paths)
// is funnelled through a single ptStep() call in a single loop (pt_segment.glsl, and the SHaRC update loop).
//
// Shadow and visibility rays (payload location 1, the instance's "shadow" group) are not traced here: they keep
// their own call sites and are unaffected.

#define PT_STEP_PRIMARY 0       // a step of the camera's ray (world or hand) up to the first surface that scatters
#define PT_STEP_BOUNCE 1        // a bounce after the first hit, the glass reflection's included
#define PT_STEP_CACHE_UPDATE 2  // a bounce of a SHaRC cache update path

// Everything that differs between the main-path traces of the pixel. Origin and direction come from the path state.
struct StepParams {
    int mode;      // PT_STEP_*
    uint flags;    // ray flags
    uint mask;     // cull mask
    uint missId;   // miss shader index
    float tMin;
    float tMax;
    bool reorder;  // regroup the threads by what they hit before the hit shaders run (SER), where allowed
    uint arrivingLobe;  // lobe the ray left its last surface by (0 diffuse, 1 reflection, 2 refraction): SER hint
};

// Bits of the reorder hint. The hit object already sorts the threads by the hit group (or miss) they ended in, which
// is the shading kind: each group records one PT_KIND_* and the raygen switches on it. The hint adds the lobe the ray
// arrived by, so threads that go on to sample the same kind of lobe (and whose next rays run alike) stay together.
#define PT_SER_HINT_BITS 2u

/**
 * Traces mainRay from its own origin along its own direction. The payload goes out with the four values the any-hit
 * shaders read from the path state (pt/pt_payload.glsl) and comes back with the recorded hit or miss.
 */
void ptTraceMain(StepParams p) {
    prismHit.coneWidth = mainRay.coneWidth;
    prismHit.coneSpread = mainRay.coneSpread;
    prismHit.stateBits = mainRay.stateBits;
    prismHit.pad0 = mainRay.pad0;
#if PT_SER_ACTIVE
    // Bounce rays, where the paths have scattered in every direction, are regrouped by what they hit before shading:
    // trace into a hit object, reorder, run the small recording shader, then shade here in the raygen
    // (ptShadeRecorded) on the regrouped threads. Reordering moves the work, not the pixel: gl_LaunchIDEXT and every
    // per-pixel image coordinate stay the same.
    if (p.reorder) {
        hitObjectNV hitObject;
        hitObjectTraceRayNV(hitObject, topLevelAS, p.flags, p.mask, 1, 1, p.missId, mainRay.origin, p.tMin,
                            mainRay.direction, p.tMax, 0);
        reorderThreadNV(hitObject, p.arrivingLobe & ((1u << PT_SER_HINT_BITS) - 1u), PT_SER_HINT_BITS);
        hitObjectExecuteShaderNV(hitObject, 0);
    } else {
        traceRayEXT(topLevelAS, p.flags, p.mask, 1, 1, p.missId, mainRay.origin, p.tMin, mainRay.direction, p.tMax, 0);
    }
#else
    traceRayEXT(topLevelAS, p.flags, p.mask, 1, 1, p.missId, mainRay.origin, p.tMin, mainRay.direction, p.tMax, 0);
#endif
}

/**
 * Shading of what the trace recorded in the payload: every main-path closest hit only records its hit
 * (world/hit/record_hit.glsl with its group's PT_KIND_*) and every main-path miss only records the miss
 * (world/hit/record_miss.rmiss: distance INF_DISTANCE), and it is shaded here, on mainRay, with the code the closest
 * hit and miss shaders used to run: same statements in the same order, same random draws from the same seed. The
 * default group, the text groups and entity_solid_z_offset_forward take the default shading
 * (pt/pt_shade_default.glsl); clouds, the End portal and gateway, the boat's water patch, world_no_reflect and the
 * emissive sheets their own (pt/pt_shade_special.glsl). The world misses (1 and 3) are evaluated by pt/pt_miss.glsl,
 * and the hand's miss (2) only ends the path.
 *
 * It sits right after the one trace and nowhere else, so each shading is compiled into the raygen once.
 */
void ptShadeRecorded(StepParams p, vec3 tracedOrigin, vec3 tracedDirection) {
    // The side channels belong to the step just traced (pt/pt_side_channels.glsl).
    ptClearStep();
    // A miss: nothing to fetch (the record holds no triangle) and none of the hit shading's state to reset, since
    // the miss shaders never touched it.
    if (prismHit.hitT >= INF_DISTANCE) {
        if (p.missId == VPT_HAND_MISS_INDEX) {
            // The hand's camera ray found no hand.
            mainRay.hitT = INF_DISTANCE;
            raySetStop(mainRay, true);
        } else {
            // The world miss draws the volumetric clouds, world_no_volumetric does not. In the miss shaders the ray
            // built-ins were the traced origin (not moved by tMin) and the unnormalised traced direction.
            ptEvalMiss(tracedOrigin, tracedDirection, p.missId == VPT_WORLD_MISS_INDEX);
        }
        return;
    }
    ptBeginRaygenShade();
    ptHitContextFromRecord(prismHit, tracedOrigin, tracedDirection);
    uint kind = prismHit.kind & PT_KIND_MASK;
    // The view mark is read as the ray arrived (the any-hits saw the same value in the payload).
    bool viewMark = mainRay.pad0 == RAY_VIEW_BOUNCE_MARK;
#if PT_SHARC_PRESHADE && SHARC_QUERY
    // Pre-shade cache order: a deep bounce the cache answers ends here, unshaded (pt/pt_sharc_preshade.glsl). Still
    // after the one trace and called nowhere else, so the lookup is compiled in once.
    if (ptSharcPreShadeQuery(kind, viewMark, p.arrivingLobe)) { return; }
#endif
    switch (kind) {
    case PT_KIND_CLOUDS: ptShadeClouds(); break;
    case PT_KIND_END_PORTAL: ptShadeEndPortal(viewMark); break;
    case PT_KIND_END_GATEWAY: ptShadeEndGateway(); break;
    case PT_KIND_WATER_MASK: ptShadeWaterMask(); break;
    case PT_KIND_NO_REFLECT: ptShadeNoReflect(); break;
    case PT_KIND_SHEET: ptShadeSheet(); break;
    // PT_KIND_DEFAULT and PT_KIND_NO_HEIGHT (the same shading without parallax).
    default: ptShadeDefault(viewMark, kind != PT_KIND_NO_HEIGHT); break;
    }
}

/**
 * One main-path step of the path state: rayState is traced and then shaded or evaluated as a miss, here in the
 * raygen. The result is in rayState; what the step hands back besides it is in g_step and g_lastMaterial.
 */
void ptStep(inout MainRay rayState, StepParams p) {
    vec3 tracedOrigin = rayState.origin;
    vec3 tracedDirection = rayState.direction;
    mainRay = rayState;
    ptTraceMain(p);
    ptShadeRecorded(p, tracedOrigin, tracedDirection);
    rayState = mainRay;
}

#endif // VPT_PT_TRACE_GLSL
