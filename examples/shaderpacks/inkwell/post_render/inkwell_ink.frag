#version 460

// Inkwell: ink outlines and paper over the finished picture (after tone mapping, before the HUD). Outlines are drawn
// where the depth jumps (silhouettes) or the normal turns (block edges), from the first-hit depth and normals the
// path tracer writes, and fade with distance so far terrain does not turn into a scribble. Then a warm paper tone and
// a fine grain. Runs after inkwell_copy, which puts the picture in post_motion.

#ifndef INKWELL_OUTLINE
#    define INKWELL_OUTLINE 1.0
#endif
#ifndef INKWELL_PAPER
#    define INKWELL_PAPER 0.35
#endif

// Pack images are in set 4 at their sampled_binding (post_motion: 4); the frame's own images in set 3 (first-hit
// depth: 1, normal and roughness: 4). Set 3 binding 0 is out:ldr itself, which this pass writes.
layout(set = 4, binding = 4) uniform sampler2D inkColour;
layout(set = 3, binding = 1) uniform sampler2D inkDepth;
layout(set = 3, binding = 4) uniform sampler2D inkNormal;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const vec3 INK = vec3(0.05, 0.045, 0.08);

struct InkSample {
    float depth;
    vec3 normal;
    bool sky;
};

InkSample fetchSample(vec2 uv) {
    InkSample s;
    vec4 n = texture(inkNormal, uv);
    // The path tracer writes no normal where the view reaches the sky.
    s.sky = dot(n.xyz, n.xyz) < 0.25;
    s.normal = s.sky ? vec3(0.0) : normalize(n.xyz);
    s.depth = s.sky ? 1e6 : max(abs(texture(inkDepth, uv).r), 1e-3);
    return s;
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec4 colour = texture(inkColour, fragTexCoord);
    vec3 c = colour.rgb;

    // Float settings: tested in the code, the preprocessor only compares integers.
    if (float(INKWELL_OUTLINE) > 0.0) {
        vec2 texel = float(INKWELL_OUTLINE) / vec2(textureSize(inkDepth, 0));
        InkSample centre = fetchSample(fragTexCoord);
        float edge = 0.0;
        const vec2 offsets[4] = vec2[](vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0));
        for (int i = 0; i < 4; i++) {
            InkSample other = fetchSample(fragTexCoord + offsets[i] * texel);
            if (centre.sky && other.sky) { continue; }
            // Silhouettes: a relative jump in depth, so near and far edges need the same contrast. Tried in the test
            // world: at 6-12% every hole in a leaf's texture (the leaf behind it is a fraction of a block further)
            // was outlined and trees turned into black blots, so only a jump of a fifth or more inks.
            float nearer = min(centre.depth, other.depth);
            float jump = abs(centre.depth - other.depth) / nearer;
            edge = max(edge, smoothstep(0.2, 0.35, jump));
            // Creases: the normal turns (only between two surfaces). The guide holds the shading normal, normal maps
            // and water ripples included: at a 50 degree turn the ripples and the bumps of stone drew ink dashes, so
            // only a turn close to a block's right angle does.
            if (!centre.sky && !other.sky) {
                edge = max(edge, smoothstep(0.7, 0.9, 1.0 - dot(centre.normal, other.normal)));
            }
        }
        // Lighter, then gone, in the distance.
        float distanceFade = 1.0 - smoothstep(48.0, 160.0, min(centre.depth, 1e5));
        c = mix(c, INK, clamp(edge * distanceFade, 0.0, 1.0) * 0.9);
    }

    if (float(INKWELL_PAPER) > 0.0) {
        // Warm paper: whites go cream, blacks lift a little towards the ink's colour, and a fine still grain.
        vec3 paper = vec3(1.0, 0.95, 0.85);
        c = mix(c, c * paper + INK * 0.25 * (1.0 - c), float(INKWELL_PAPER));
        vec2 pixel = floor(fragTexCoord * vec2(textureSize(inkColour, 0)));
        c += (hash(pixel) - 0.5) * 0.035 * float(INKWELL_PAPER);
    }

    outColor = vec4(clamp(c, 0.0, 1.0), colour.a);
}
