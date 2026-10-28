#ifndef RADIANTE_HOOK_UNDERWATER_GLSL
#define RADIANTE_HOOK_UNDERWATER_GLSL

// Radiante Example Pack: the picture ripples while the camera is under water, and its edges darken a little. The
// ripple reads the picture a few pixels aside (hookPostGradeInput), moving with the world's clock; 0 turns it off.
// Runs in the post_hook_grade pass, which this pack turns on for its colour grade anyway.
#ifndef EXAMPLE_UNDERWATER_RIPPLE
#    define EXAMPLE_UNDERWATER_RIPPLE 0.5
#endif

void hookUnderwater(inout vec3 color, vec2 uv) {
    float strength = float(EXAMPLE_UNDERWATER_RIPPLE);
    if (strength <= 0.0) return;
    float seconds = mod(worldUBO.gameTime * 1200.0, 600.0);
    vec2 offset = vec2(sin(uv.y * 40.0 + seconds * 2.1), cos(uv.x * 34.0 + seconds * 1.7)) * 0.004 * strength;
    color = texture(hookPostGradeInput, clamp(uv + offset, vec2(0.0), vec2(1.0))).rgb;
    vec2 centred = uv - 0.5;
    color *= 1.0 - 0.6 * strength * dot(centred, centred);
}

#endif
