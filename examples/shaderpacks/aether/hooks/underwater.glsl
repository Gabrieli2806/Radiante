#ifndef RADIANTE_HOOK_UNDERWATER_GLSL
#define RADIANTE_HOOK_UNDERWATER_GLSL

// Aether: with the camera under water the picture wavers gently (read a little aside from hookPostGradeInput, on two
// slow interfering waves), light from the surface shimmers faintly towards the top of the view, and the edges sink
// into a deeper blue-green. Underwater Shimmer sets all three; 0 turns it off. Runs in the post_hook_grade pass,
// which the pack turns on for its grade, before hooks/post_grade.glsl grades the result.
#include "aether/aether_common.glsl"

void hookUnderwater(inout vec3 color, vec2 uv) {
    float strength = clamp(float(AETHER_UNDERWATER_WARP), 0.0, 1.0);
    if (strength <= 0.0) { return; }
    float time = mod(aetherSeconds(), 3600.0);
    vec2 offset = vec2(sin(uv.y * 23.0 + time * 1.4 + sin(uv.x * 7.0 + time * 0.6) * 1.5),
                       cos(uv.x * 19.0 + time * 1.1 + sin(uv.y * 5.0 - time * 0.5) * 1.5)) *
                  (0.0022 * strength);
    color = texture(hookPostGradeInput, clamp(uv + offset, vec2(0.0), vec2(1.0))).rgb;
    float shimmer = 0.5 + 0.5 * sin(uv.x * 31.0 + time * 1.7 + sin(uv.y * 13.0 + time) * 2.0);
    color *= 1.0 + 0.05 * strength * shimmer * (1.0 - uv.y);
    vec2 centred = uv - 0.5;
    color = mix(color, color * vec3(0.55, 0.85, 0.90), clamp(dot(centred, centred) * 1.6 * strength, 0.0, 1.0));
}

#endif
