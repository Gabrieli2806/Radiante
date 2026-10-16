#ifndef VPT_PT_HIT_CONTEXT_GLSL
#define VPT_PT_HIT_CONTEXT_GLSL

// Everything the surface shading knows about the hit it shades: the ray that was traced, how far along it the hit
// is, which triangle of which geometry was hit and where on it, the instance's transforms, and the triangle's
// vertices. The shading reads these from g_hit and never from the hit-stage built-ins, so the same source can run in
// a closest-hit shader (which fills g_hit from its built-ins, below) and, later, in the ray generation shader (which
// will fill it from a recorded hit).
// Needs util/vertex.glsl (vertex types, triangle loads, getGeometryBufferIndex) before it.

struct HitContext {
    vec3 rayOrigin;           // the traced origin and direction, as given to the trace (direction not normalised)
    vec3 rayDir;
    float hitT;               // distance along rayDir to the hit
    uint instanceId;          // instance custom index (also indexes lastObjToWorldMats)
    uint geometryBufferIndex; // getGeometryBufferIndex(instance, geometry)
    uint primitiveId;
    vec3 bary;                // (1 - u - v, u, v)
    mat3 objectToWorld;       // the instance's object-to-world rotation and scale
    mat3 normalMatrix;        // takes object-space normals to world space (used normalised only)
    PositionVertex p0;
    PositionVertex p1;
    PositionVertex p2;
    MaterialVertex m0;
    MaterialVertex m1;
    MaterialVertex m2;
    uint packedData;          // m0.packedData: the surface flags of the triangle
    vec3 planeHitWorldPos;    // rayOrigin + rayDir * hitT, the hit on the triangle's plane
};

HitContext g_hit;

#ifdef PT_IN_HIT_STAGE
hitAttributeEXT vec2 attribs;

// Fills g_hit from the closest-hit built-ins and loads the triangle, as the top of the old default.rchit main() did.
void ptHitContextFromBuiltins() {
    g_hit.rayOrigin = gl_WorldRayOriginEXT;
    g_hit.rayDir = gl_WorldRayDirectionEXT;
    g_hit.hitT = gl_HitTEXT;
    g_hit.instanceId = gl_InstanceCustomIndexEXT;
    g_hit.geometryBufferIndex = getGeometryBufferIndex(gl_InstanceCustomIndexEXT, gl_GeometryIndexEXT);
    g_hit.primitiveId = gl_PrimitiveID;

    uint i0, i1, i2;
    loadTriangleIndices(g_hit.geometryBufferIndex, gl_PrimitiveID, i0, i1, i2);
    loadTriangleMaterial(g_hit.geometryBufferIndex, i0, i1, i2, g_hit.m0, g_hit.m1, g_hit.m2);
    loadTrianglePositions(g_hit.geometryBufferIndex, i0, i1, i2, g_hit.p0, g_hit.p1, g_hit.p2);

    g_hit.bary = vec3(1.0 - (attribs.x + attribs.y), attribs.x, attribs.y);
    g_hit.objectToWorld = mat3(gl_ObjectToWorld3x4EXT);
    g_hit.normalMatrix = transpose(mat3(gl_WorldToObject3x4EXT));
    g_hit.packedData = g_hit.m0.packedData;
    g_hit.planeHitWorldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
}
#endif

#endif // VPT_PT_HIT_CONTEXT_GLSL
