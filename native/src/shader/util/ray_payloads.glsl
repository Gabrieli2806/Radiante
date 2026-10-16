#ifndef RAY_PAYLOAD_GLSL
#define RAY_PAYLOAD_GLSL

#include "common/mapping.hpp"

// What a closest-hit or miss shader found, written for the raygen to shade from (the raygen-shading migration).
// The raygen cannot see the vertex/material address tables or the hit built-ins, so the hit shader copies here
// everything the shading needs to rebuild the hit: which triangle, where on it, the buffer addresses to load its
// vertices from, and the object-to-world rotation. The hit position itself is origin + direction * hitT.
// While MainRay is still the payload this block rides along at its end (see rayRecordedBit in util/ray.glsl);
// once the shading has moved, the payload becomes this record plus the any-hits' inputs.
struct PrismHitRecord {
    T_FLOAT hitT;                 // gl_HitTEXT; the miss writes INF_DISTANCE and nothing else
    T_UINT kind;                  // PT_KIND_* in bits 0-7, PT_KIND_FRONT_FACE_BIT for a front-face hit
    T_UINT instanceCustomIndex;   // gl_InstanceCustomIndexEXT (also indexes the last object-to-world matrices)
    T_UINT geometryBufferIndex;   // getGeometryBufferIndex(instanceCustomIndex, gl_GeometryIndexEXT)
    T_UINT primitiveIndex;        // gl_PrimitiveID
    T_VEC2 barycentrics;          // the triangle hit attributes
    uvec2 positionAddress;        // positionBufferAddrs.addrs[geometryBufferIndex], tagged form kept as is
    uvec2 materialAddress;        // materialBufferAddrs.addrs[geometryBufferIndex]
    uvec2 lastPositionAddress;    // lastPositionBufferAddrs.addrs[geometryBufferIndex] (previous-frame positions)
    T_VEC3 objectToWorld0;        // rows of mat3(gl_ObjectToWorld3x4EXT); no translation is needed because the
    T_VEC3 objectToWorld1;        // raygen already knows the world-space hit position
    T_VEC3 objectToWorld2;
};

// Which raygen shading branch a recorded hit takes. One value per closest-hit body the pack has; the hit group
// names in configs.json stay as they are and only choose which recording stub runs.
const uint PT_KIND_DEFAULT = 0u;     // default.rchit: chunks, entities, LOD, the text groups
const uint PT_KIND_NO_HEIGHT = 1u;   // no_height.rchit: the default shading without parallax
const uint PT_KIND_CLOUDS = 2u;
const uint PT_KIND_END_PORTAL = 3u;
const uint PT_KIND_END_GATEWAY = 4u;
const uint PT_KIND_WATER_MASK = 5u;
const uint PT_KIND_NO_REFLECT = 6u;
const uint PT_KIND_SHEET = 7u;       // transparent_only.rchit: lightning, beacon beam
const uint PT_KIND_MASK = 0xFFu;
const uint PT_KIND_FRONT_FACE_BIT = 1u << 8u;

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

    // Transitional: filled by the recording hit stubs once they exist and only meaningful when rayRecordedBit
    // is set. Appended last so every field above keeps its offset.
    PrismHitRecord rec;
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
