#ifndef RAY_PAYLOAD_GLSL
#define RAY_PAYLOAD_GLSL

// Data carried along rays. The members are grouped as a vec3 followed by a scalar so each group fills one 16-byte
// slot; the member order is the layout every shader of the pack agrees on and must not change. The T_* types come
// from common/mapping.hpp so the C++ side can mirror the same structs.

#include "common/mapping.hpp"

// The state of one camera path: where its current segment starts and goes, what it has gathered so far, and the
// ray cone used for texture level-of-detail. stateBits holds the bounce count and the flags in util/ray.glsl.
struct MainRay {
    T_VEC3 origin;
    T_FLOAT hitT;

    T_VEC3 direction;
    T_UINT seed;

    T_VEC3 radiance;
    T_FLOAT coneWidth;

    T_VEC3 throughput;
    T_FLOAT coneSpread;

    T_VEC3 prevScenePos;
    T_UINT hasPrevScenePos;

    T_VEC3 normal;
    T_UINT stateBits;

    T_VEC3 directLightRadiance;
    T_UINT pad0;
};

// The material of the last surface shaded, as the bounce logic needs it to continue the path.
struct MaterialInfo {
    T_VEC4 albedoValue;
    T_VEC3 f0;
    T_FLOAT roughness;
    T_FLOAT metallic;
    T_FLOAT transmission;
    T_FLOAT ior;
    T_FLOAT emission;
};

// A visibility ray towards a light. Any-hit shaders along the way multiply throughput by what they let through.
struct ShadowRay {
    T_VEC3 radiance;
    T_UINT insideBoat;
    T_VEC3 throughput;
    T_UINT pad0;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // Any-hit shaders run in no particular order, so the water boundary nearest the origin is tracked on its own
    // instead of folding it into the glass throughput, and its factor is applied once, after traversal has shown
    // the sky is visible.
    T_VEC3 waterTransmission;
    T_FLOAT nearestWaterT;
#endif
};

#endif
