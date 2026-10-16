#ifndef VPT_PT_HIT_CONTEXT_GLSL
#define VPT_PT_HIT_CONTEXT_GLSL

// Everything the surface shading knows about the hit it shades: the ray that was traced, how far along it the hit
// is, which triangle of which geometry was hit and where on it, the instance's transforms, and the triangle's
// vertices. The shading runs in the ray generation shader, which has no hit built-ins: g_hit is filled from the hit
// the recording closest hit wrote into the payload (ptHitContextFromRecord), and the shading reads only g_hit.
// Needs util/vertex.glsl (vertex types, the triangle loads) and pt/pt_payload.glsl before it.

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

// Fills g_hit from the hit the recording closest hit wrote down (world/hit/record_hit.glsl) and loads the triangle,
// in the order the old closest-hit shading did it. origin and direction are the ones the ray was traced with: a
// closest hit's gl_WorldRayOriginEXT / gl_WorldRayDirectionEXT are exactly those.
void ptHitContextFromRecord(PrismHit rec, vec3 origin, vec3 direction) {
    g_hit.rayOrigin = origin;
    g_hit.rayDir = direction;
    g_hit.hitT = rec.hitT;
    g_hit.instanceId = rec.instanceCustomIndex;
    g_hit.geometryBufferIndex = rec.geometryBufferIndex;
    g_hit.primitiveId = rec.primitiveIndex;

    uint i0, i1, i2;
    loadTriangleIndices(g_hit.geometryBufferIndex, g_hit.primitiveId, i0, i1, i2);
    loadTriangleMaterial(g_hit.geometryBufferIndex, i0, i1, i2, g_hit.m0, g_hit.m1, g_hit.m2);
    loadTrianglePositions(g_hit.geometryBufferIndex, i0, i1, i2, g_hit.p0, g_hit.p1, g_hit.p2);

    g_hit.bary = vec3(1.0 - (rec.barycentrics.x + rec.barycentrics.y), rec.barycentrics.x, rec.barycentrics.y);
    g_hit.objectToWorld = mat3(rec.objectToWorld0, rec.objectToWorld1, rec.objectToWorld2);
    // The closest hit used transpose(mat3(gl_WorldToObject3x4EXT)), which is transpose(inverse(objectToWorld)) for
    // the matrix above. Built here from the cofactors (the columns below are det * inverse-transpose) divided by
    // the determinant: a mirrored instance keeps its sign, and the normal keeps the length it had, so the
    // degenerate-triangle fallback of normalizeF triggers exactly where it did. A singular matrix gives a zero
    // matrix (that fallback) instead of infinities.
    vec3 c0 = g_hit.objectToWorld[0];
    vec3 c1 = g_hit.objectToWorld[1];
    vec3 c2 = g_hit.objectToWorld[2];
    vec3 cof0 = cross(c1, c2);
    float det = dot(c0, cof0);
    float invDet = det != 0.0 ? 1.0 / det : 0.0;
    g_hit.normalMatrix = mat3(cof0, cross(c2, c0), cross(c0, c1)) * invDet;
    g_hit.packedData = g_hit.m0.packedData;
    g_hit.planeHitWorldPos = origin + direction * rec.hitT;
}

#endif // VPT_PT_HIT_CONTEXT_GLSL
