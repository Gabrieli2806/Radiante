#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

// Fragment stage for post-pass text. The post text mode picks how the glyph texture is read:
//   1, 4     solid colour (no texture)
//   2, 5, 7  single-channel glyph atlas, red replicated to all channels
//   3, 6, 8  full RGBA texture
//   other    RGBA texture only if the vertex asks for one
// Modes 7 and 8 are pulled a hair towards the camera in depth so they win against coplanar text.
// Besides colour it writes a linear depth (metres, for the effects after it) and a depth spread over 1000 m.

#include "common/shared.hpp"

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(set = 1, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(location = 0) in vec3 pos;
layout(location = 1) flat in uint useNorm;
layout(location = 2) in vec3 norm;
layout(location = 3) flat in uint useColorLayer;
layout(location = 4) in vec4 colorLayer;
layout(location = 5) flat in uint useTexture;
layout(location = 6) flat in uint useOverlay;
layout(location = 7) in vec2 textureUV;
layout(location = 8) flat in ivec2 overlayUV;
layout(location = 9) flat in uint useGlint;
layout(location = 10) flat in uint textureID;
layout(location = 11) in vec2 glintUV;
layout(location = 12) flat in uint glintTexture;
layout(location = 13) flat in uint useLight;
layout(location = 14) flat in ivec2 lightUV;
layout(location = 15) in vec4 lightMapColor;
layout(location = 16) in vec4 overlayColor;
layout(location = 17) flat in uint postTextMode;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out float outLinearDepth;

vec4 baseColour() {
    uint mode = postTextMode;
    bool solid = mode == 1u || mode == 4u;
    bool singleChannel = mode == 2u || mode == 5u || mode == 7u;
    bool fullColour = mode == 3u || mode == 6u || mode == 8u;
    if (solid) { return vec4(1.0); }
    if (singleChannel) { return texture(textures[nonuniformEXT(textureID)], textureUV).rrrr; }
    if (fullColour || useTexture > 0) { return texture(textures[nonuniformEXT(textureID)], textureUV); }
    return vec4(1.0);
}

void main() {
    vec4 colour = baseColour();
    if (useColorLayer > 0) { colour *= colorLayer; }
    // Overlay alpha is how much of the original colour survives (damage flash and similar tints).
    if (useOverlay > 0) { colour.rgb = mix(overlayColor.rgb, colour.rgb, overlayColor.a); }
    if (colour.a < 0.1) { discard; }

    fragColor = useLight == 0 ? colour : colour * lightMapColor;

    // Rotation-only view transform: the position is already camera-relative.
    float viewDepth = -(mat4(mat3(worldUBO.cameraEffectedViewMat)) * vec4(pos, 1.0)).z;
    float depth = clamp(viewDepth / 1000.0, 0.0, 1.0);
    bool pullForward = postTextMode == 7u || postTextMode == 8u;
    gl_FragDepth = pullForward ? max(depth - 1e-4, 0.0) : depth;
    outLinearDepth = max(viewDepth, 0.0);
}
