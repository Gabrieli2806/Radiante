#ifndef VPT_PT_SIDE_CHANNELS_GLSL
#define VPT_PT_SIDE_CHANNELS_GLSL

// What a shaded hit or an evaluated miss hands back to the rest of the ray generation shader besides the path state:
// the material it was shaded with, a small auxiliary pair (water origin bias and medium flags), a request to trace a
// reflection off a surface the view went straight through (glass, the side of water), and the stars' visibility
// through the volumetric clouds.
//
// When the shading ran in closest-hit shaders these travelled through fields of the payload with a second meaning
// (the request in `normal` and `directLightRadiance`, the pair in `pad0`, the stars in `directLightRadiance.x`) and
// through the material-state image. Now that the shading runs in the raygen, they are plain variables that the code
// after each step (pt/pt_segment.glsl, world.rgen, pt/pt_sharc_update.glsl) reads directly.
//
// Needs util/ray.glsl (MaterialInfo) and the global `MainRay mainRay` before it.

// A reflection the ray generation shader should trace once the view behind a pass-through surface is done.
// No request: a zero weight and a zero direction (the raygen takes a request only with weight > 1e-4 and a unit
// direction).
struct PtReflectRequest {
    vec3 dir;        // the reflected direction
    vec3 weight;     // the path throughput times the face's Fresnel term
    uint mediumBits; // Bedrock glass: in water (1), inside glass (2), glass inside water (4), as the view arrived
};

// The side channels of one main-path step. ptShadeRecorded (pt/pt_trace.glsl) clears them before every hit or miss
// is shaded, so they always belong to the step just traced: no request, a zero pair and no star visibility unless
// that hit or miss sets them, as the payload fields they replace started each trace.
struct PtStep {
    PtReflectRequest reflectRequest;
    vec2 aux;               // x: the water origin bias, y: the water flag or the Bedrock glass medium bits
    float starVisibility;   // a world miss: how much of the stars the volumetric clouds let through (0 to 1)
};

PtStep g_step;

void ptClearStep() {
    g_step.reflectRequest.dir = vec3(0.0);
    g_step.reflectRequest.weight = vec3(0.0);
    g_step.reflectRequest.mediumBits = 0u;
    g_step.aux = vec2(0.0);
    g_step.starVisibility = 0.0;
}

// The material of the last surface shaded on this thread. Every shade that has a material overwrites it (the water
// patch of a boat has none and leaves it alone), and the code after a step reads whatever the last one left: the
// material-state image this replaces behaved the same way. world.rgen keeps the first hit's material when each camera
// ray's segment ends, so the glass reflection traced after it no longer replaces it. It starts as the cleared
// material for a read that no shade preceded. Not rounded to half precision as the image was.
MaterialInfo g_lastMaterial = MaterialInfo(vec4(0.0), vec3(0.0), 0.0, 0.0, 0.0, 0.0, 0.0);

// The emission the default shading added to the path's radiance (throughput included), summed over the surfaces
// shaded since world.rgen last cleared it. world.rgen clears it before each camera ray's segment and reads it after,
// so it holds what the first hit (and any mirror in front of it) glowed with: the NRD path stores its denoised
// channels without it and hands it to the emission image instead, so compose counts it once.
vec3 g_seenEmission = vec3(0.0);

// Records the material the hit was shaded with.
void ptStoreMaterial(vec4 albedoValue,
                     vec3 f0,
                     float roughness,
                     float metallic,
                     float transmission,
                     float ior,
                     float emission) {
    g_lastMaterial.albedoValue = albedoValue;
    g_lastMaterial.f0 = f0;
    g_lastMaterial.roughness = roughness;
    g_lastMaterial.metallic = metallic;
    g_lastMaterial.transmission = transmission;
    g_lastMaterial.ior = ior;
    g_lastMaterial.emission = emission;
}

// Records the auxiliary pair (x: the water origin bias, y: the water flag or the Bedrock glass medium bits).
void ptStoreAux(vec2 aux) {
    g_step.aux = aux;
}

// Asks the raygen to trace a reflection off this pass-through surface.
// The path state's normal is set to the reflected direction as well (zero when no reflection is asked for): that is
// the normal a pass-through hit has always reported, and two readers of the path state see it there. The G-buffer
// normal of a camera view that runs out of steps on a pass-through surface (world.rgen's first-hit outputs), and the
// cache cell a SHaRC update path files such a hit under (pt/pt_sharc_update.glsl).
void ptRequestReflection(vec3 dir, vec3 weight, uint mediumBits) {
    g_step.reflectRequest.dir = dir;
    g_step.reflectRequest.weight = weight;
    g_step.reflectRequest.mediumBits = mediumBits;
    mainRay.normal = dir;
}

// A pass-through surface that asks for no reflection.
void ptClearReflectionRequest() {
    ptRequestReflection(vec3(0.0), vec3(0.0), 0u);
}

#endif // VPT_PT_SIDE_CHANNELS_GLSL
