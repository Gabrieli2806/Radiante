#ifndef VPT_PT_PAYLOAD_GLSL
#define VPT_PT_PAYLOAD_GLSL

// The main-path ray payload (location 0): what the ray generation shader hands to a trace and what the hit or miss
// shader hands back. The surface shading, the miss evaluation and the whole path state (MainRay) live in the ray
// generation shader, so the payload only carries what the stages in between need:
// - in: the four values the any-hit shaders read (texture level of detail from the ray cone, the path's state bits,
//   the view mark), written by the raygen before every main-path trace (ptTraceMain, pt/pt_trace.glsl);
// - out: what was hit, written by the recording closest hit (world/hit/record_hit.glsl), or a distance of
//   INF_DISTANCE and nothing else from the recording miss (world/hit/record_miss.rmiss). The raygen cannot see the
//   hit built-ins, so the closest hit copies here what the shading needs to rebuild the hit (pt/pt_hit_context.glsl);
//   the per-geometry buffer addresses the raygen looks up itself (set 1 bindings 4, 5 and 6).
// Every stage that touches location 0 includes this file, so they all declare the same type. 20 dwords (80 bytes;
// MainRay, the payload before, was 28 plus the record).
//
// The shadow and visibility rays keep their own payload (ShadowRay, location 1, util/ray_payloads.glsl).

#include "util/ray.glsl"

struct PrismHit {
    // In.
    float coneWidth;            // the ray cone at the origin: default.rahit and text.rahit pick their texture level
    float coneSpread;           //   with coneWidth + hit distance * coneSpread
    uint stateBits;             // the path's state bits (bounce, inside boat, camera path, in glass, ignore water)
    uint pad0;                  // RAY_VIEW_BOUNCE_MARK when the ray is part of a view chain, else 0
    // Out.
    float hitT;                 // gl_HitTEXT; INF_DISTANCE from the recording miss
    uint kind;                  // PT_KIND_* in bits 0-7, PT_KIND_FRONT_FACE_BIT for a front-face hit
    uint instanceCustomIndex;   // gl_InstanceCustomIndexEXT (also indexes the last object-to-world matrices)
    uint geometryBufferIndex;   // blasOffsets.offsets[instance] + gl_GeometryIndexEXT
    uint primitiveIndex;        // gl_PrimitiveID
    vec2 barycentrics;          // the triangle hit attributes
    vec3 objectToWorld0;        // the columns of mat3(gl_ObjectToWorld3x4EXT); no translation is needed, the raygen
    vec3 objectToWorld1;        //   knows the hit position already (origin + direction * hitT)
    vec3 objectToWorld2;
};

// Which raygen shading a recorded hit takes, one per recording stub (world/hit/*.rchit). The hit group names in
// configs.json stay as they were and only choose the stub.
const uint PT_KIND_DEFAULT = 0u;     // the default group, the four text groups, every entity layer
const uint PT_KIND_NO_HEIGHT = 1u;   // entity_solid_z_offset_forward: the default shading without parallax
const uint PT_KIND_CLOUDS = 2u;
const uint PT_KIND_END_PORTAL = 3u;
const uint PT_KIND_END_GATEWAY = 4u;
const uint PT_KIND_WATER_MASK = 5u;
const uint PT_KIND_NO_REFLECT = 6u;  // world_no_reflect
const uint PT_KIND_SHEET = 7u;       // lightning, beacon_beam
const uint PT_KIND_MASK = 0xFFu;
const uint PT_KIND_FRONT_FACE_BIT = 1u << 8u;

// The path state tests the any-hit shaders make, on the state bits the payload carries in (the same bits as
// MainRay's, util/ray.glsl).
uint rayBounce(PrismHit hit) { return hit.stateBits & rayBounceMask; }
bool rayInsideBoat(PrismHit hit) { return (hit.stateBits & rayInsideBoatBit) != 0u; }
bool rayCameraPath(PrismHit hit) { return (hit.stateBits & rayCameraPathBit) != 0u; }
bool rayInGlass(PrismHit hit) { return (hit.stateBits & rayInGlassBit) != 0u; }
bool rayIgnoreWaterSelf(PrismHit hit) { return (hit.stateBits & rayIgnoreWaterSelfBit) != 0u; }

#endif // VPT_PT_PAYLOAD_GLSL
