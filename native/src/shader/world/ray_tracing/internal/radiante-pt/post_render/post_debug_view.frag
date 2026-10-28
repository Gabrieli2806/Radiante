#version 460
#extension GL_GOOGLE_include_directive : require

// The chosen debug view over the whole picture (common/debug_view.glsl): radiante_debug_view as the ray tracing stage
// filled it, at the render resolution, scaled to the output. Last in the post stage, so nothing draws over it.

#include "common/debug_view.glsl"

layout(set = 4, binding = RADIANTE_DEBUG_VIEW_SAMPLED_BINDING) uniform sampler2D radianteDebugView;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(texture(radianteDebugView, fragTexCoord).rgb, 1.0);
}
