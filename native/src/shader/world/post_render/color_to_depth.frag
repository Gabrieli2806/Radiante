#version 460

// How far each pixel's camera ray went before its first hit.
layout(set = 0, binding = 2, r16f) uniform readonly image2D firstHitDistance;

// Camera rays end at this distance. The depth written is the hit's distance as a fraction of it; the radiante-pt
// pack's post_render shaders write the depth of what they draw the same way, so the depth test compares like with like.
const float CAMERA_RAY_LENGTH = 1000.0;

void main() {
    float distanceToHit = imageLoad(firstHitDistance, ivec2(gl_FragCoord.xy)).r;
    gl_FragDepth = clamp(distanceToHit / CAMERA_RAY_LENGTH, 0.0, 1.0);
}
