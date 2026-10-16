#ifndef VPT_RECORD_HIT_GLSL
#define VPT_RECORD_HIT_GLSL

// The body of every main-path closest hit (world/hit/*.rchit): it only writes down what was hit, in the out half of
// the payload (pt/pt_payload.glsl), and the ray generation shader shades it (ptShadeRecorded, pt/pt_trace.glsl).
// Everything the raygen cannot read itself goes in the record: the hit built-ins (distance, instance, geometry,
// primitive, barycentrics, front or back face, the instance's rotation). The raygen rebuilds the hit context from it
// (pt/pt_hit_context.glsl).
//
// The including stub supplies #version and the extensions (ray tracing, include, int64) and defines PT_HIT_KIND,
// which raygen shading its group takes (PT_KIND_* in pt/pt_payload.glsl).

#ifndef PT_HIT_KIND
#    error "record_hit.glsl: the including shader must define PT_HIT_KIND"
#endif

#include "common/shared.hpp"
#include "pt/pt_payload.glsl"

layout(set = 1, binding = 1) readonly buffer BLASOffsets {
    uint offsets[];
}
blasOffsets;

layout(location = 0) rayPayloadInEXT PrismHit prismHit;
hitAttributeEXT vec2 attribs;

void main() {
    // The same index the shading used to compute for itself (getGeometryBufferIndex in util/vertex.glsl).
    uint geometryBufferIndex = blasOffsets.offsets[gl_InstanceCustomIndexEXT] + gl_GeometryIndexEXT;

    prismHit.hitT = gl_HitTEXT;
    prismHit.kind = PT_HIT_KIND | (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT ? PT_KIND_FRONT_FACE_BIT : 0u);
    prismHit.instanceCustomIndex = gl_InstanceCustomIndexEXT;
    prismHit.geometryBufferIndex = geometryBufferIndex;
    prismHit.primitiveIndex = gl_PrimitiveID;
    prismHit.barycentrics = attribs;
    // The shading reads the instance's rotation as mat3(gl_ObjectToWorld3x4EXT); its three columns are sent as they
    // are, so the raygen rebuilds exactly that matrix. No translation: the raygen has the hit position already.
    mat3 objectToWorld = mat3(gl_ObjectToWorld3x4EXT);
    prismHit.objectToWorld0 = objectToWorld[0];
    prismHit.objectToWorld1 = objectToWorld[1];
    prismHit.objectToWorld2 = objectToWorld[2];
}

#endif // VPT_RECORD_HIT_GLSL
