#version 460
#extension GL_GOOGLE_include_directive : require

// The hookPostGrade hook point (hooks/post_grade.glsl) over the finished picture, and before it hookUnderwater
// (hooks/underwater.glsl) while the camera is under water, with radiante-pt's own under water camera effects ahead of
// both. Runs when a pack sets the execution_post global RADIANTE_HOOK_POST_GRADE, and with the camera under water
// while Underwater Camera Effects is above 0, after post_hook_copy has put the picture in post_motion.

// The post stage's uniforms, so the hook can read the pack's live settings (worldUBO.packParams) and the weather and
// biome (common/pack_world.glsl, which reads skyUBO), and the bloom pyramid (radianteBloom).
#include "common/pack_post.glsl"

// post_motion, where post_hook_copy put the picture: pack images are sampled from set 4, at their sampled_binding
// (configs.json). Set 3 binding 0 is always out:ldr itself, which this pass writes.
layout(set = 4, binding = 4) uniform sampler2D hookPostGradeInput;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

#include "hooks/underwater.glsl"
#include "hooks/post_grade.glsl"

// Underwater Camera Effects (configs.json): 0 off, 1 as made, 2 twice as strong.
#ifndef VPT_UNDERWATER_CAMERA_EFFECTS
#    define VPT_UNDERWATER_CAMERA_EFFECTS 1.0
#endif
// How far the water between the eye and the picture bends it, as a share of the screen's height: a slow swaying, not
// a ripple. Its strength is the effects' setting times these.
const float UNDERWATER_WOBBLE = 0.0012;
// How much the edges of the view soften (a blur up to this many pixels at the corners) and darken (the share of the
// light the corners lose): the eye's view through water is sharpest straight ahead.
const float UNDERWATER_EDGE_BLUR = 2.5;
const float UNDERWATER_VIGNETTE = 0.3;
// The faint flicker of sunlight focused by the waves above, at most this share of the picture's brightness, by day,
// strongest near the surface and gone about UNDERWATER_SHIMMER_DEPTH blocks down.
const float UNDERWATER_SHIMMER = 0.05;
const float UNDERWATER_SHIMMER_DEPTH = 12.0;

// A cheap moving caustic net: two bent sine grids folded into thin bright lines, 0..1.
float underwaterShimmer(vec2 p, float time) {
    vec2 q = p + 0.35 * vec2(sin(p.y * 1.7 + time * 0.9), sin(p.x * 1.3 - time * 0.7));
    float a = abs(sin(q.x * 2.1 + time * 0.6) + sin(q.y * 2.4 - time * 0.5));
    float b = abs(sin((q.x + q.y) * 1.6 - time * 0.8) + sin((q.x - q.y) * 1.9 + time * 0.4));
    return pow(clamp(1.0 - 0.5 * min(a, b), 0.0, 1.0), 6.0);
}

// The camera's own view through water: swaying, soft at the edges, darker at the corners, with the sun's caustics
// flickering faintly over it by day. Only while the camera is under water; the picture is display colour.
vec3 underwaterCamera(vec2 uv, float strength) {
    vec2 size = vec2(textureSize(hookPostGradeInput, 0));
    float aspect = size.x / max(size.y, 1.0);
    float time = worldUBO.gameTime * 1200.0;
    vec2 p = vec2(uv.x * aspect, uv.y);
    // Two slow crossing swells, more at the edges than in the middle, where one looks.
    vec2 centred = uv - 0.5;
    float edge = clamp(dot(centred * vec2(aspect, 1.0), centred * vec2(aspect, 1.0)) * 2.2, 0.0, 1.0);
    vec2 wobble = vec2(sin(p.y * 9.0 + time * 1.3) + 0.5 * sin((p.x + p.y) * 14.0 - time * 1.7),
                       sin(p.x * 8.0 - time * 1.1) + 0.5 * sin((p.x - p.y) * 12.0 + time * 1.5));
    vec2 offset = wobble * (UNDERWATER_WOBBLE * strength * (0.4 + 0.6 * edge)) * vec2(1.0 / aspect, 1.0);
    vec2 at = clamp(uv + offset, vec2(0.0), vec2(1.0));
    vec3 color = texture(hookPostGradeInput, at).rgb;
    // The soft edge: four taps on a small cross, wider towards the corners.
    float radius = UNDERWATER_EDGE_BLUR * strength * edge;
    if (radius > 0.25) {
        vec2 texel = radius / size;
        vec3 blurred = color + texture(hookPostGradeInput, at + vec2(texel.x, 0.0)).rgb +
                       texture(hookPostGradeInput, at - vec2(texel.x, 0.0)).rgb +
                       texture(hookPostGradeInput, at + vec2(0.0, texel.y)).rgb +
                       texture(hookPostGradeInput, at - vec2(0.0, texel.y)).rgb;
        color = mix(color, blurred * 0.2, clamp(edge * strength, 0.0, 1.0));
    }
    // Sunlight's caustics, by day and near the surface, a little brighter where the picture already is.
    vec3 sun = skyUBO.sunDirection;
    float day = worldUBO.skyType == 1 && dot(sun, sun) > 1e-8 ? smoothstep(0.0, 0.3, normalize(sun).y) : 0.0;
    float depth = max(skyUBO.waterExtinction.w - 1.0, 0.0);
    float shimmerAmount = UNDERWATER_SHIMMER * strength * day * (1.0 - skyUBO.rainGradient * 0.7) *
                          exp(-depth / UNDERWATER_SHIMMER_DEPTH);
    if (shimmerAmount > 0.0) { color *= 1.0 + shimmerAmount * underwaterShimmer(p * 6.0, time); }
    // The vignette.
    color *= 1.0 - clamp(UNDERWATER_VIGNETTE * strength * edge * edge, 0.0, 0.9);
    return color;
}

void main() {
    vec4 color = texture(hookPostGradeInput, fragTexCoord);
    vec3 graded = color.rgb;
    float underwaterStrength = clamp(float(VPT_UNDERWATER_CAMERA_EFFECTS), 0.0, 2.0);
    if (skyUBO.cameraSubmersionType == 1u && underwaterStrength > 0.0) {
        graded = underwaterCamera(fragTexCoord, underwaterStrength);
    }
#ifndef RADIANTE_HOOK_UNDERWATER_DEFAULT
    // The camera under water (skyUBO.cameraSubmersionType: 0 lava, 1 water, 2 powder snow, 3 none).
    if (skyUBO.cameraSubmersionType == 1u) { hookUnderwater(graded, fragTexCoord); }
#endif
    hookPostGrade(graded, fragTexCoord);
    outColor = vec4(graded, color.a);
}
