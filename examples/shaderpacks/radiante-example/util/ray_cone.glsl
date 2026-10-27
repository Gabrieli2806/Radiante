#ifndef RAY_CONE_GLSL
#define RAY_CONE_GLSL

// Ray cones for texture level-of-detail. Each camera ray is treated as a thin cone one pixel wide; how wide it has
// grown where it hits a surface, compared with how much surface one texel covers, gives the mip level to read.

// Horizontal field of view of a projection matrix: P[0][0] is 1 / tan(fovX / 2).
float fovXFromProj(mat4 P) {
    float xScale = P[0][0];
    return 2.0 * atan(1.0 / abs(xScale));
}

// Vertical field of view of a projection matrix: P[1][1] is 1 / tan(fovY / 2).
float fovYFromProj(mat4 P) {
    float yScale = P[1][1];
    return 2.0 * atan(1.0 / abs(yScale));
}

// The spread of a one-pixel cone: the tangent of the angle one pixel subtends, taking the wider of the two axes so
// that non-square pixels never under-filter.
float coneSpreadFromFov(float fovY, float fovX, vec2 screenSize) {
    float pixelsDown = screenSize.y;
    float pixelsAcross = screenSize.x;

    float anglePerPixelY = atan(tan(fovY * 0.5) / pixelsDown);
    float anglePerPixelX = atan(tan(fovX * 0.5) / pixelsAcross);

    float widest = max(anglePerPixelX, anglePerPixelY);
    return tan(widest);
}

// How far a triangle's surface moves per unit of u and per unit of v, from its three corners. Solves the 2x2 system
// that maps the corners' UV differences onto their position differences. A triangle whose UVs are degenerate (no
// area in texture space) falls back to its two edge directions.
void computedposduDv(vec3 pos0, vec3 pos1, vec3 pos2, vec2 uv0, vec2 uv1, vec2 uv2, out vec3 dposdu, out vec3 dposdv) {
    vec3 edgeA = pos1 - pos0;
    vec3 edgeB = pos2 - pos0;
    vec2 uvEdgeA = uv1 - uv0;
    vec2 uvEdgeB = uv2 - uv0;

    float uvArea = uvEdgeA.x * uvEdgeB.y - uvEdgeA.y * uvEdgeB.x;
    if (abs(uvArea) < 1e-12) {
        dposdu = normalize(edgeA);
        dposdv = normalize(edgeB);
        return;
    }

    float invArea = 1.0 / uvArea;
    dposdu = (edgeA * uvEdgeB.y - edgeB * uvEdgeA.y) * invArea;
    dposdv = (-edgeA * uvEdgeB.x + edgeB * uvEdgeA.x) * invArea;
}

// Mip level for a cone of the given world-space radius: the radius measured in texels along u and v (whichever is
// larger), then log2 of that. Never below level 0.
float lodWithCone(sampler2D tex, vec2 uv, float coneRadiusWorld, vec3 dposdu, vec3 dposdv) {
    float worldPerU = max(length(dposdu), 1e-6);
    float worldPerV = max(length(dposdv), 1e-6);

    float spanU = coneRadiusWorld / worldPerU;
    float spanV = coneRadiusWorld / worldPerV;

    ivec2 baseSize = textureSize(tex, 0);
    float texelsCovered = max(spanU * float(baseSize.x), spanV * float(baseSize.y));

    float level = max(log2(max(texelsCovered, 1e-6)), 0.0);

    return level;
}

#endif
