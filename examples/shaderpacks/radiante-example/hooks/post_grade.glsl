#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// Radiante Example Pack: a colour grade over the finished picture. Warmer highlights, slightly cooler shadows, a
// gentle S curve for contrast. Turned on by RADIANTE_HOOK_POST_GRADE in configs.json; with the setting off it changes
// nothing.
void hookPostGrade(inout vec3 color, vec2 uv) {
#if EXAMPLE_GRADE != 0
    vec3 c = clamp(color, 0.0, 1.0);
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    // Split toning: shadows toward teal, highlights toward amber.
    vec3 shadowTint = vec3(0.95, 1.0, 1.06);
    vec3 highlightTint = vec3(1.12, 1.02, 0.84);
    c *= mix(shadowTint, highlightTint, smoothstep(0.05, 0.7, luma));
    // S curve around mid grey: blend of the input and a smoothstep, about 15% more contrast in the mids.
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.45);
    // Saturation held where it was: the curve already deepens colour, and Minecraft's greens go neon quickly.
    float graded = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(graded), c, 0.95);
    color = clamp(c, 0.0, 1.0);
#endif
}

#endif
