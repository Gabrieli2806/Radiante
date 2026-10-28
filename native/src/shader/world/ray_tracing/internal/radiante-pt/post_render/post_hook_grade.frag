#version 460
#extension GL_GOOGLE_include_directive : require

// The hookPostGrade hook point (hooks/post_grade.glsl) over the finished picture. Runs only when a pack sets the
// execution_post global RADIANTE_HOOK_POST_GRADE, after post_hook_copy has put the picture in post_motion.

#include "common/shared.hpp"

// The post stage's uniforms, so the hook can read the pack's settings (worldUBO.packParams) and the weather and biome
// (common/pack_world.glsl, which reads skyUBO).
layout(set = 1, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 1, binding = 1) uniform SkyUniform {
    SkyUBO skyUBO;
};

// post_motion, where post_hook_copy put the picture: pack images are sampled from set 4, at their sampled_binding
// (configs.json). Set 3 binding 0 is always out:ldr itself, which this pass writes.
layout(set = 4, binding = 4) uniform sampler2D hookPostGradeInput;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

#include "hooks/post_grade.glsl"

void main() {
    vec4 color = texture(hookPostGradeInput, fragTexCoord);
    vec3 graded = color.rgb;
    hookPostGrade(graded, fragTexCoord);
    outColor = vec4(graded, color.a);
}
