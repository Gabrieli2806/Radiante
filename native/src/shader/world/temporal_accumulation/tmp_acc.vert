#version 460

// One triangle that covers the whole target, texCoord running from 0 to 1 across it.
layout(location = 0) out vec2 texCoord;

void main() {
    // Vertices 0, 1 and 2 sit at uv (0, 0), (0, 2) and (2, 0); the order keeps the triangle front facing, and the
    // pipeline culls back faces.
    vec2 uv = vec2(gl_VertexIndex & 2, (gl_VertexIndex << 1) & 2);
    texCoord = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
