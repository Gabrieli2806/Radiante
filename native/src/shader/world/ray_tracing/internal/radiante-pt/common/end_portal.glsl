#include "common/constants.glsl"
#ifndef VPT_END_PORTAL_GLSL
#define VPT_END_PORTAL_GLSL

// How densely the star sheet repeats across a block. Vanilla projects the stars from the camera, so they slide over
// the block as the view turns; a path tracer also sees the portal in reflections, where a screen projection makes
// no sense, so the sheet is pinned to world space instead. 0.25 made the stars about half their vanilla size at
// typical viewing distances, hence the smaller value.
const float END_PORTAL_WORLD_SCALE = 0.125;

// Projective coordinates for a portal surface, taken from the two world axes the face lies in (xz for the flat
// portal, zy or xy for an upright gateway face), so the sheet runs along the face and not through it.
vec4 endPortalProjection(vec3 worldPos, vec3 normal) {
    vec2 planeCoords;
    if (abs(normal.y) > 0.5) {
        planeCoords = worldPos.xz;
    } else if (abs(normal.x) > 0.5) {
        planeCoords = worldPos.zy;
    } else {
        planeCoords = worldPos.xy;
    }
    return vec4(planeCoords * END_PORTAL_WORLD_SCALE, 0.0, 1.0);
}

// The vanilla End portal look: the sky texture tinted by the deepest color, then one star layer per iteration,
// each scaled, rotated by a fixed per-layer angle and scrolled over time at its own speed.
vec3 computeEndPortalColor(vec4 texProj0, int iterations, uint endSkyTextureID, uint endPortalTextureID, float gameTime) {
    const uint missingTexture = 0xFFFFFFFFu;
    vec3 accumulated = vec3(0.0);
    if (endSkyTextureID != missingTexture)
        accumulated += textureProj(textures[nonuniformEXT(endSkyTextureID)], texProj0).rgb * VPT_COLORS[0];
    for (int layerIndex = 0; layerIndex < iterations; layerIndex++) {
        if (endPortalTextureID == missingTexture) { continue; }
        float depth = float(layerIndex + 1);
        // Row-major as written: a sideways offset per layer and a scroll that speeds up with depth.
        mat4 scroll = mat4(
            1.0, 0.0, 0.0, 17.0 / depth,
            0.0, 1.0, 0.0, (2.0 + depth / 1.5) * (gameTime * 1.5),
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0
        );
        float angle = radians((depth * depth * 4321.0 + depth * 9.0) * 2.0);
        float c = cos(angle);
        float s = sin(angle);
        mat2 spin = mat2(c, -s, s, c);
        mat2 zoom = mat2((4.5 - depth / 4.0) * 2.0);
        mat4 layerTransform = mat4(zoom * spin) * scroll * VPT_SCALE_TRANSLATE;
        accumulated += textureProj(textures[nonuniformEXT(endPortalTextureID)], texProj0 * layerTransform).rgb *
                       VPT_COLORS[layerIndex];
    }
    return accumulated;
}
#endif
