#version 330
#extension GL_ARB_separate_shader_objects : require

// A screen filling triangle just in front of the far plane (Minecraft's depth is 1 at the camera and 0 far away):
// drawn with a "nearer than this" depth test it covers the traced world and leaves the sky. See ModBackdrop.

layout(location = 0) out vec2 texCoord;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * vec2(2, 2) + vec2(-1, -1), 1.0e-6, 1);
    texCoord = uv;
}
