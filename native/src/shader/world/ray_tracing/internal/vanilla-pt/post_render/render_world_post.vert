#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

// Vertex stage for world geometry rasterised over the path-traced image (things the tracer does not handle).
// Same space handling as render_text.vert: inCoordinate 0 = world space, 1 = view space, 2 = camera-relative;
// output is camera-relative in world orientation. Per-vertex light-map and overlay colours are fetched here.

#include "common/shared.hpp"

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(set = 1, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in uint inUseNorm;
layout(location = 2) in vec3 inNorm;
layout(location = 3) in uint inUseColorLayer;
layout(location = 4) in vec4 inColorLayer;
layout(location = 5) in uint inUseTexture;
layout(location = 6) in uint inUseOverlay;
layout(location = 7) in vec2 inTextureUV;
layout(location = 8) in ivec2 inOverlayUV;
layout(location = 9) in uint inUseGlint;
layout(location = 10) in uint inTextureID;
layout(location = 11) in vec2 inGlintUV;
layout(location = 12) in uint inGlintTexture;
layout(location = 13) in uint inUseLight;
layout(location = 14) in ivec2 inLightUV;
layout(location = 15) in uint inCoordinate;
layout(location = 16) in vec3 inPostBase;

layout(location = 0) out vec3 outPos;
layout(location = 1) flat out uint outUseNorm;
layout(location = 2) out vec3 outNorm;
layout(location = 3) flat out uint outUseColorLayer;
layout(location = 4) out vec4 outColorLayer;
layout(location = 5) flat out uint outUseTexture;
layout(location = 6) flat out uint outUseOverlay;
layout(location = 7) out vec2 outTextureUV;
layout(location = 8) flat out ivec2 outOverlayUV;
layout(location = 9) flat out uint outUseGlint;
layout(location = 10) flat out uint outTextureID;
layout(location = 11) out vec2 outGlintUV;
layout(location = 12) flat out uint outGlintTexture;
layout(location = 13) flat out uint outUseLight;
layout(location = 14) flat out ivec2 outLightUV;
layout(location = 15) out vec4 lightMapColor;
layout(location = 16) out vec4 overlayColor;

void main() {
    mat3 viewToWorld = mat3(worldUBO.cameraViewMatInv);
    vec3 position = inPos + inPostBase;

    switch (inCoordinate) {
        case 0u:
            // World space: make it camera-relative; normals are already in world orientation.
            position -= vec3(worldUBO.cameraPos.xyz);
            outNorm = inNorm;
            break;
        case 1u:
            // View space: rotate position and normal into world orientation.
            position = viewToWorld * position;
            outNorm = normalize(viewToWorld * inNorm);
            break;
        case 2u:
            // Already camera-relative.
            outNorm = inNorm;
            break;
    }
    outPos = position;
    gl_Position = worldUBO.cameraProjMat * worldUBO.cameraEffectedViewMat * vec4(position, 1.0);

    // Pass-through attributes.
    outUseNorm = inUseNorm;
    outUseColorLayer = inUseColorLayer;
    outColorLayer = inColorLayer;
    outUseTexture = inUseTexture;
    outUseOverlay = inUseOverlay;
    outTextureUV = inTextureUV;
    outOverlayUV = inOverlayUV;
    outUseGlint = inUseGlint;
    outTextureID = inTextureID;
    outGlintUV = inGlintUV;
    outGlintTexture = inGlintTexture;
    outUseLight = inUseLight;
    outLightUV = inLightUV;

    // Light-map coordinates come in 1/16 texel units.
    lightMapColor = inUseLight > 0
        ? texelFetch(textures[nonuniformEXT(worldUBO.lightMapTextureID)], inLightUV / 16, 0)
        : vec4(0.0);
    overlayColor = inUseOverlay > 0
        ? texelFetch(textures[nonuniformEXT(worldUBO.overlayTextureID)], inOverlayUV, 0)
        : vec4(0.0);
}
