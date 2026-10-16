#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "util/disney.glsl"
#include "util/random.glsl"
#include "util/ray_cone.glsl"
#include "util/ray.glsl"
#include "util/util.glsl"
#include "common/shared.hpp"

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(set = 1, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout(set = 1, binding = 1) readonly buffer BLASOffsets {
    uint offsets[];
}
blasOffsets;

layout(set = 1, binding = 2) readonly buffer IndexBufferAddr {
    uint64_t addrs[];
}
indexBufferAddrs;

layout(set = 1, binding = 3) readonly buffer LastIndexBufferAddr {
    uint64_t addrs[];
}
lastIndexBufferAddrs;

layout(set = 1, binding = 8) readonly buffer LastObjToWorldMat {
    mat4 mat[];
}
lastObjToWorldMats;

layout(set = 1, binding = 7) readonly buffer TextureMappingBuffer {
    TextureMapping mapping;
};

layout(set = 2, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 2, binding = 1) uniform LastWorldUniform {
    WorldUBO lastWorldUbo;
};

layout(set = 2, binding = 2) uniform SkyUniform {
    SkyUBO skyUBO;
};

layout(set = 3, binding = 1, rgba8) uniform image2D diffuseAlbedoImage;
layout(set = 3, binding = 2, rgba8) uniform image2D specularAlbedoImage;
layout(set = 3, binding = 3, rgba16f) uniform image2D normalRoughnessImage;
layout(set = 3, binding = 4, rg16f) uniform image2D motionVectorImage;
layout(set = 3, binding = 5, r16f) uniform image2D linearDepthImage;

layout(std430, buffer_reference, buffer_reference_align = 8) readonly buffer IndexBuffer {
    uint indices[];
}
indexBuffer;

#include "util/vertex.glsl"

layout(location = 0) rayPayloadInEXT MainRay mainRay;
hitAttributeEXT vec2 attribs;

#include "common/end_portal.glsl"

// The light the End portal gives off, per unit of surface (radiance): a teal of about the strength of a lamp's.
const vec3 END_PORTAL_GLOW = vec3(0.35, 0.75, 0.85) * 5.0;

void main() {
    vec3 viewDir = -mainRay.direction;

    uint instanceID = gl_InstanceCustomIndexEXT;
    uint geometryID = gl_GeometryIndexEXT;

    uint geometryBufferIndex = getGeometryBufferIndex(instanceID, geometryID);

    uint i0;
    uint i1;
    uint i2;
    loadTriangleIndices(geometryBufferIndex, gl_PrimitiveID, i0, i1, i2);

    PositionVertex p0;
    PositionVertex p1;
    PositionVertex p2;
    loadTrianglePositions(geometryBufferIndex, i0, i1, i2, p0, p1, p2);

    vec3 baryCoords = vec3(1.0 - (attribs.x + attribs.y), attribs.x, attribs.y);
    vec3 localPos = baryCoords.x * p0.pos + baryCoords.y * p1.pos + baryCoords.z * p2.pos;
    vec3 worldPos = vec4(localPos, 1.0) * gl_ObjectToWorld3x4EXT;

    vec3 faceNormal = normalize(cross(p1.pos - p0.pos, p2.pos - p0.pos));
    // Where the point is in the world, not around the camera (the positions here are camera-relative): anchored to
    // the camera, the starfield slid along with every step and with the view bobbing while walking. Wrapped every
    // 1024 blocks, a whole number of the pattern's repeats, so far from the origin it keeps its precision.
    vec3 anchoredPos = worldPos + vec3(mod(worldUBO.cameraPos.xyz, dvec3(1024.0)));
    vec4 texProj0 = endPortalProjection(anchoredPos, faceNormal);
    vec3 color = computeEndPortalColor(texProj0, 15, worldUBO.endSkyTextureID, worldUBO.endPortalTextureID, worldUBO.gameTime);

    // The colour above is already what vanilla puts on the screen: a nearly black field with a scattering of
    // bright stars. Multiplying it turned the whole block into a flat glowing slab, which is what it is supposed
    // not to look like - the dark part of a portal is dark.
    // And it is a picture in screen colours, not light: taken as it is for light, its dark field came out a glowing
    // grey under the exposure. Converted to linear light, the field is near black again and only the stars shine.
    color = pow(max(color, vec3(0.0)), vec3(2.2));
    mainRay.radiance += color * mainRay.throughput;
    // It is a light source (level 15 in vanilla): a ray that comes to it off another surface takes a cool glow from
    // it besides its stars, so the frame and the room around a portal are lit teal. The camera sees only the
    // starfield; the glow stays off the portal itself, which would otherwise be a flat lit slab. So does a view off
    // a mirror or the water, which shows the portal as the camera would.
    if (!rayCameraPath(mainRay) && mainRay.pad0 != RAY_VIEW_BOUNCE_MARK) {
        mainRay.radiance += END_PORTAL_GLOW * worldUBO.emissionBrightness * mainRay.throughput;
    }
    mainRay.hitT = gl_HitTEXT;
    // Its own face, and a surface that reflects nothing (fully rough, no reflectance) for the denoisers: with no
    // normal and no roughness given they took the starfield for a perfect mirror, and carried it along with the view
    // as they would a reflection.
    vec3 worldNormal = normalize(vec4(faceNormal, 0.0) * gl_ObjectToWorld3x4EXT);
    mainRay.normal = dot(worldNormal, viewDir) < 0.0 ? -worldNormal : worldNormal;
    rayStoreMaterial(mainRay, vec4(0.0), vec3(0.0), 1.0, 0.0, 0.0, 1.0, 0.0);
    raySetNoisy(mainRay, false);
    mainRay.hasPrevScenePos = 0u;
    raySetStop(mainRay, true);
}
