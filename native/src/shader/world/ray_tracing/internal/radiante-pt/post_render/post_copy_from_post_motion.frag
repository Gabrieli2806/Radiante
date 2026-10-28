#version 460

// Copies post_motion back into out:ldr, after the motion blur. post_copy.frag cannot do it: it reads set 3 binding 0,
// which is always out:ldr itself, so the copy wrote the unblurred picture over itself and the blur was lost whenever
// depth of field was off. Pack images are sampled from set 4 at their sampled_binding (configs.json): post_motion's is 4.
layout(set = 4, binding = 4) uniform sampler2D postMotionInput;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(postMotionInput, fragTexCoord);
}
