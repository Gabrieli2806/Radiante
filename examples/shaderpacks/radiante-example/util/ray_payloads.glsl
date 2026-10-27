#ifndef RAY_PAYLOAD_GLSL
#define RAY_PAYLOAD_GLSL

#include "common/mapping.hpp"

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

struct MaterialInfo {
    T_VEC4 albedoValue;
    T_VEC3 f0;
    T_FLOAT roughness;
    T_FLOAT metallic;
    T_FLOAT transmission;
    T_FLOAT ior;
    T_FLOAT emission;
};

struct ShadowRay {
    T_VEC3 radiance;
    T_UINT insideBoat;
    T_VEC3 throughput;
    T_UINT pad0;
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    // Any-hit order is unspecified. Keep the nearest boundary's factor separate from
    // glass throughput and apply it once, after traversal proves celestial visibility.
    T_VEC3 waterTransmission;
    T_FLOAT nearestWaterT;
#endif
};

#endif
