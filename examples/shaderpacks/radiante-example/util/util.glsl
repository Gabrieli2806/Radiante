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

#endif
