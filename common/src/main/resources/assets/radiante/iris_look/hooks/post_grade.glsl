#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// Radiante look (made from an Iris pack's settings): a colour grade over the finished picture. Saturation, dark tones
// losing their colour, a warm or cool balance, contrast around mid grey and a vignette. Written for Radiante; the
// values come from the converted pack's options (IrisLookImport), and each is a setting.
#ifndef LOOK_SATURATION
#    define LOOK_SATURATION 1.0
#endif
#ifndef LOOK_DARK_DESATURATION
#    define LOOK_DARK_DESATURATION 0.0
#endif
#ifndef LOOK_WARMTH
#    define LOOK_WARMTH 0.0
#endif
#ifndef LOOK_CONTRAST
#    define LOOK_CONTRAST 0.0
#endif
#ifndef LOOK_VIGNETTE
#    define LOOK_VIGNETTE 0.0
#endif

void hookPostGrade(inout vec3 color, vec2 uv) {
    const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
    vec3 c = clamp(color, 0.0, 1.0);
    float y = dot(c, luma);
    // Saturation, less of it in the dark: shadows go grey before they go black, as night and caves do in raster packs.
    float saturation = float(LOOK_SATURATION) * mix(1.0, smoothstep(0.0, 0.25, y), float(LOOK_DARK_DESATURATION));
    c = max(mix(vec3(y), c, saturation), 0.0);
    // Warm (positive) or cool (negative), lighter tones more than darker ones.
    float warmth = float(LOOK_WARMTH) * smoothstep(0.1, 0.8, y);
    c *= vec3(1.0 + 0.10 * warmth, 1.0 + 0.02 * warmth, 1.0 - 0.10 * warmth);
    // Contrast: towards an S curve around mid grey (positive) or flatter (negative).
    c = clamp(c, 0.0, 1.0);
    c = mix(c, c * c * (3.0 - 2.0 * c), float(LOOK_CONTRAST) * 2.0);
    // Vignette: corners darker, the centre untouched.
    vec2 centred = uv - 0.5;
    c *= 1.0 - float(LOOK_VIGNETTE) * smoothstep(0.2, 0.75, dot(centred, centred) * 2.0);
    color = clamp(c, 0.0, 1.0);
}

#endif
