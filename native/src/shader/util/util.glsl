#ifndef UTIL_GLSL
#define UTIL_GLSL

#include "color_space.glsl"

// Screen-space motion of a point for the denoisers and the upscaler: where last frame's camera put it, in pixels,
// minus where it is drawn now. Ray tracing stages only (uses the launch size as the screen size).
vec2 computeCameraMotionVector(mat4 prevMVP, vec2 pixelCenter, vec4 motionOrigin) {
    highp vec4 previousClip = prevMVP * motionOrigin;
    previousClip.xy /= previousClip.w;
    previousClip.xy = (previousClip.xy * 0.5 + 0.5) * vec2(gl_LaunchSizeEXT.xy);
    vec2 motion = previousClip.xy - pixelCenter;
    return motion;
}

// The Householder matrix I - 2 n n^T: reflects a vector across the plane with the given unit normal. Built column
// by column.
mat3 buildMirrorMatrix(vec3 normal) {
    return mat3(-2.0 * (vec3(normal.x) * normal) + vec3(1.0, 0.0, 0.0), //
                -2.0 * (vec3(normal.y) * normal) + vec3(0.0, 1.0, 0.0), //
                -2.0 * (vec3(normal.z) * normal) + vec3(0.0, 0.0, 1.0));
}

// Reinhard compression driven by the brightest channel, so the hue is kept while the value is squeezed into 0..1.
vec3 reinhardMax(vec3 color) {
    float peak = max(1e-7, max(max(color.x, color.y), color.z));
    float compressed = peak / (peak + 1);
    return color * (compressed / peak);
}

#endif
