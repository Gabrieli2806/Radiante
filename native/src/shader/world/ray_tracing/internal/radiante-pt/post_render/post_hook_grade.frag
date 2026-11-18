#version 460
#extension GL_GOOGLE_include_directive : require

// The hookPostGrade hook point (hooks/post_grade.glsl) over the finished picture, and before it hookUnderwater
// (hooks/underwater.glsl) while the camera is under water. Runs only when a pack sets the execution_post global
// RADIANTE_HOOK_POST_GRADE, after post_hook_copy has put the picture in post_motion.

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

void main() {
    vec4 color = texture(hookPostGradeInput, fragTexCoord);
    vec3 inputColor = max(color.rgb, vec3(0.0));
    vec3 graded = inputColor;
#ifndef RADIANTE_HOOK_UNDERWATER_DEFAULT
    // The camera under water (skyUBO.cameraSubmersionType: 0 lava, 1 water, 2 powder snow, 3 none).
    if (skyUBO.cameraSubmersionType == 1u) { hookUnderwater(graded, fragTexCoord); }
#endif
    hookPostGrade(graded, fragTexCoord);
    // Packs work on 0..1 display colours and usually end with clamp(c, 0.0, 1.0). When HDR output is active and the
    // input held headroom above paper white (> 1.0) that the hook clipped at 1.0, carry that headroom on scaled by
    // the hook's 0..1 result so letterbox bars stay black, vignettes dim it and split toning tints it.
    float inPeak = max(inputColor.r, max(inputColor.g, inputColor.b));
    float outPeak = max(graded.r, max(graded.g, graded.b));
    if (inPeak > 1.0 && outPeak <= 1.001) {
        graded *= mix(1.0, inPeak, smoothstep(0.5, 1.0, outPeak));
    }
    outColor = vec4(graded, color.a);
}
