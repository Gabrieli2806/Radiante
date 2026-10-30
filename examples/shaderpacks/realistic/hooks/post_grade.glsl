#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// Realistic: the contrast and saturation of the old Realistic renderer profile (1.08 and 0.92), which set them on the
// tone mapping module. A pack cannot change tone mapping's settings, so they are applied here, over the finished
// picture, the way the Bedrock tone mapper (Radiante's default) applies its own: contrast about 0.18 in linear light,
// clamped, saturation about the luminance, then taken 90% of the way from the mapped colour (Bedrock's intensity).
// With the tone mapper at its defaults (contrast and saturation 1), a pixel the curve left inside 0 to 1 comes out as
// that profile showed it; a clipped highlight stays clipped. At 1 and 1 the picture is left as it is.
//
// Both are live settings, read from the world uniform each frame, so they are tested with if, never #if.
#ifndef REALISTIC_CONTRAST
#    define REALISTIC_CONTRAST 1.08
#endif
#ifndef REALISTIC_SATURATION
#    define REALISTIC_SATURATION 0.92
#endif

// The tone mapper's luminance weights (Rec. 709).
const vec3 REALISTIC_LUMA = vec3(0.2126, 0.7152, 0.0722);
// How far Bedrock's tone mapping material takes the graded colour from the mapped one (BEDROCK_TONE_MAPPING_INTENSITY
// in tone_mapping.frag).
const float REALISTIC_GRADE_INTENSITY = 0.9;

// The exact sRGB curve both ways, as the tone mapper encodes the picture, so a neutral grade gives back the same value.
vec3 realisticDecodeSrgb(vec3 display) {
    vec3 low = display / 12.92;
    vec3 high = pow((display + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(display, vec3(0.04045)));
}

vec3 realisticEncodeSrgb(vec3 linear) {
    vec3 low = linear * 12.92;
    vec3 high = 1.055 * pow(max(linear, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThan(linear, vec3(0.0031308)));
}

void hookPostGrade(inout vec3 color, vec2 uv) {
    float contrast = float(REALISTIC_CONTRAST);
    float saturation = float(REALISTIC_SATURATION);
    if (contrast == 1.0 && saturation == 1.0) {
        return;
    }
    vec3 mapped = realisticDecodeSrgb(clamp(color, 0.0, 1.0));
    vec3 graded = clamp((mapped - 0.18) * contrast + 0.18, 0.0, 1.0);
    float luma = dot(graded, REALISTIC_LUMA);
    graded = max(luma + (graded - luma) * max(saturation, 0.0), vec3(0.0));
    color = clamp(realisticEncodeSrgb(mapped + REALISTIC_GRADE_INTENSITY * (graded - mapped)), 0.0, 1.0);
}

#endif
