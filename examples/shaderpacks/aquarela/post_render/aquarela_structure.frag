#version 460

// Aquarela, first post pass: which way the picture's edges run, for the brush (aquarela_brush.frag). It writes the
// structure tensor of the finished picture's colour (Kyprianidis et al., "Image and Video Abstraction by Anisotropic
// Kuwahara Filtering", 2009) into aquarela_flow at a quarter of the picture's size: each texel sums the colour
// gradients of the 4 x 4 pixels it covers, and the brush reads it with linear filtering, so the field comes out
// smoothed over a dozen pixels without a blur pass of its own.
//
// It reads out:ldr directly (set 3, binding 0): only a pass that writes out:ldr must not sample it, and this one
// writes aquarela_flow.

layout(set = 3, binding = 0) uniform sampler2D aquarelaPicture;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outFlow;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(aquarelaPicture, 0));
    // The 4 x 4 pixels under this texel and a ring of one pixel round them, at pixel centres (the texel's centre is
    // a pixel corner of the full picture, so the centres sit at half-pixel offsets).
    vec3 colour[36];
    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 6; x++) {
            vec2 offset = vec2(float(x) - 2.5, float(y) - 2.5);
            colour[y * 6 + x] = clamp(texture(aquarelaPicture, fragTexCoord + offset * texel).rgb, 0.0, 1.0);
        }
    }
    // E, F, G: the sums of gx.gx, gx.gy and gy.gy over the colour channels (central differences), averaged.
    vec3 tensor = vec3(0.0);
    for (int y = 1; y < 5; y++) {
        for (int x = 1; x < 5; x++) {
            vec3 gx = 0.5 * (colour[y * 6 + x + 1] - colour[y * 6 + x - 1]);
            vec3 gy = 0.5 * (colour[(y + 1) * 6 + x] - colour[(y - 1) * 6 + x]);
            tensor += vec3(dot(gx, gx), dot(gx, gy), dot(gy, gy));
        }
    }
    outFlow = vec4(tensor * (1.0 / 16.0), 1.0);
}
