#ifndef VPT_PT_SIDE_CHANNELS_GLSL
#define VPT_PT_SIDE_CHANNELS_GLSL

// What a shaded hit hands back to the ray generation shader besides the path state: the material it was shaded
// with, a small auxiliary pair (water origin bias and medium flags), and a request to trace a reflection off a
// surface the view went straight through (glass, the side of water).
//
// Today these travel through fields of the main payload and the material-state image, each with a second meaning:
// the reflection request sits in `normal` and `directLightRadiance`, which a pass-through hit does not otherwise
// use, and the auxiliary pair in `pad0`. Written through the functions below instead, the shading code says what it
// hands back, and the same calls work whether the shading runs in a closest-hit shader or in the ray generation
// shader: g_step holds the explicit values, and until the raygen reads them directly they are still mirrored into
// the old fields, so every reader sees exactly what it saw before.
//
// Needs util/ray.glsl (MaterialInfo, rayStoreMaterial, rayStoreAux) and a global `MainRay mainRay` before it.

// A reflection the ray generation shader should trace once the view behind a pass-through surface is done.
// No request: a zero weight and a zero direction (the raygen takes a request only with weight > 1e-4 and a unit
// direction).
struct PtReflectRequest {
    vec3 dir;        // the reflected direction
    vec3 weight;     // the path throughput times the face's Fresnel term
    uint mediumBits; // Bedrock glass: in water (1), inside glass (2), glass inside water (4), as the view arrived
};

// The side channels of the step being shaded.
struct PtStep {
    PtReflectRequest reflectRequest;
    vec2 aux;
};

PtStep g_step;

#ifndef PT_IN_HIT_STAGE
// The material of the last surface shaded on this thread, as the material-state image held it: every store
// overwrites it, so the "last writer wins" behaviour of the image is kept exactly (also where a later shade
// overwrote the material of the first hit before the raygen read it). Not rounded to half precision as the image is.
MaterialInfo g_lastMaterial;
#endif

// Records the material the hit was shaded with. A closest-hit shader writes it to the material-state image, which
// the raygen reads back after the trace. The raygen keeps it in g_lastMaterial and, as long as hits shaded in a
// closest-hit shader still hand theirs over through the image, writes the image too so the two never disagree.
void ptStoreMaterial(vec4 albedoValue,
                     vec3 f0,
                     float roughness,
                     float metallic,
                     float transmission,
                     float ior,
                     float emission) {
#ifndef PT_IN_HIT_STAGE
    g_lastMaterial.albedoValue = albedoValue;
    g_lastMaterial.f0 = f0;
    g_lastMaterial.roughness = roughness;
    g_lastMaterial.metallic = metallic;
    g_lastMaterial.transmission = transmission;
    g_lastMaterial.ior = ior;
    g_lastMaterial.emission = emission;
#endif
    rayStoreMaterial(mainRay, albedoValue, f0, roughness, metallic, transmission, ior, emission);
}

// Records the auxiliary pair (x: the water origin bias, y: the water flag or the Bedrock glass medium bits).
// Mirrored into pad0, where the raygen reads it with rayLoadAux.
void ptStoreAux(vec2 aux) {
    g_step.aux = aux;
    rayStoreAux(mainRay, aux);
}

// Asks the raygen to trace a reflection off this pass-through surface. Mirrored into normal / directLightRadiance,
// where the raygen reads it today.
void ptRequestReflection(vec3 dir, vec3 weight, uint mediumBits) {
    g_step.reflectRequest.dir = dir;
    g_step.reflectRequest.weight = weight;
    g_step.reflectRequest.mediumBits = mediumBits;
    mainRay.normal = dir;
    mainRay.directLightRadiance = weight;
}

// A pass-through surface that asks for no reflection (the zero normal and direct light it always wrote).
void ptClearReflectionRequest() {
    ptRequestReflection(vec3(0.0), vec3(0.0), 0u);
}

#endif // VPT_PT_SIDE_CHANNELS_GLSL
