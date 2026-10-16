#version 460

// One triangle over the whole target, with corners (-1, -1), (-1, 3) and (3, -1) in this order. The order makes it
// front facing: the pipeline culls back faces.
void main() {
    vec2 corner = vec2(gl_VertexIndex == 2 ? 3.0 : -1.0, gl_VertexIndex == 1 ? 3.0 : -1.0);
    gl_Position = vec4(corner, 0.0, 1.0);
}
