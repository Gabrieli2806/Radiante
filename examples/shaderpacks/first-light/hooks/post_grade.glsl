#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// First Light: the punchy finish of the 2019 and 2020 RTX showcases: a wide bloom round the sun, lamps and bright
// windows, firmer contrast with deep blacks, and vivid colour. Turned on by RADIANTE_HOOK_POST_GRADE in configs.json;
// RADIANTE_POST_BLOOM there has Radiante draw its bloom pyramid, which common/pack_post.glsl gives this hook
// (radianteBloom). The colour here is a display colour, 0 to 1, after Radiante's tone mapping and its eye adaptation.
//
// All the strengths are live settings: their defines read the world uniform, so they are tested with if, not #if.
#ifndef FL_BLOOM
#    define FL_BLOOM 0.35
#endif
#ifndef FL_CONTRAST
#    define FL_CONTRAST 0.35
#endif
#ifndef FL_SATURATION
#    define FL_SATURATION 1.2
#endif

const vec3 FL_POST_LUMA = vec3(0.2126, 0.7152, 0.0722);

// Interleaved gradient noise, for a dither too small to see, against banding in the deep blue sky.
float flIgn(vec2 pixel) {
    return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y));
}

void hookPostGrade(inout vec3 color, vec2 uv) {
    float bloom = max(float(FL_BLOOM), 0.0);
    if (bloom > 0.0) {
        // As radianteAddBloom does: decoded to linear, the bloom added as light, encoded again.
        vec3 linear = pow(max(color, vec3(0.0)), vec3(2.2));
        vec3 glow = radianteBloom(uv) * bloom;
        color = pow(linear + glow / (1.0 + glow), vec3(1.0 / 2.2));
    }

    vec3 c = clamp(color, 0.0, 1.0);
    float contrast = clamp(float(FL_CONTRAST), 0.0, 1.0);
    if (contrast > 0.0) {
        // An S curve on the luminance, the hue kept: darker shadows, brighter highlights, the middle where it was.
        float l = dot(c, FL_POST_LUMA);
        float s = l * l * (3.0 - 2.0 * l);
        c *= mix(l, s, contrast) / max(l, 1e-5);
    }
    c = mix(vec3(dot(c, FL_POST_LUMA)), c, float(FL_SATURATION));

    vec2 pixel = uv * vec2(textureSize(hookPostGradeInput, 0));
    c += (flIgn(pixel) - 0.5) / 255.0;
    color = clamp(c, 0.0, 1.0);
}

#endif
