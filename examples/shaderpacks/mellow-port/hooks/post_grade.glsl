#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// Mellow port: Mellow's final grade. Ported from Mellow by TheCMK (MIT, LICENSE-MELLOW): apply_vibrance,
// apply_saturation, apply_contrast and decode_lut in shaders/lib/tonemap.glsl, in the order of shaders/program/final.fsh
// and composite8.fsh, with the default VIBRANCE 1.14, SATURATION 1.0 and CONTRAST 0.97.
//
// What changed for Radiante: Mellow multiplies its picture by EXPOSURE 2.3 and puts it through an ACES curve before
// this; here Radiante's own exposure and tone mapping have run already, and the grade works on the display colour
// they give, which is what Mellow's grade works on too (it raises the picture to 1 / 2.2 first). Mellow's LUT is off
// by default, as in Mellow; it reads the tone-mapped colour in display terms and its output is the colour shown, so
// the LUT's texel is taken as it is. Mellow's vignette is 0 by default and its film grain off, so neither is ported.
// Vibrance, saturation, contrast and the LUT are live settings ("runtime": true): their defines read the world
// uniform, so the LUT is picked with if, not #if.
#ifndef MELLOW_VIBRANCE
#    define MELLOW_VIBRANCE 1.14
#endif
#ifndef MELLOW_SATURATION
#    define MELLOW_SATURATION 1.0
#endif
#ifndef MELLOW_CONTRAST
#    define MELLOW_CONTRAST 0.97
#endif
#ifndef MELLOW_LUT
#    define MELLOW_LUT 0
#endif

// Mellow's 512x512 LUTs (a 64-level cube cut into 8 x 8 slices of blue): img/luts/vintage.png, mellow.png and
// punchy.png, Mellow's LUT_FILE 8, 9 and 10.
layout(set = 4, binding = 50) uniform sampler2D mellowLutVintage;
layout(set = 4, binding = 51) uniform sampler2D mellowLutMellow;
layout(set = 4, binding = 52) uniform sampler2D mellowLutPunchy;

float mellowGradeLuminance(vec3 c) {
    return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}

// Interleaved gradient noise, Mellow's ign(): it dithers the LUT lookup and the final picture.
float mellowIgn(vec2 pos) {
    return fract(52.9829189 * fract(0.06711056 * pos.x + 0.00583715 * pos.y));
}

vec3 mellowApplyVibrance(vec3 color, float intensity) {
    float mn = min(color.r, min(color.g, color.b));
    float mx = max(color.r, max(color.g, color.b));
    float sat = (1.0 - clamp(mx - mn, 0.0, 1.0)) * clamp(1.0 - mx, 0.0, 1.0) * mellowGradeLuminance(color) * 5.0;
    vec3 lightness = vec3((mn + mx) * 0.5);
    return mix(color, mix(lightness, color, intensity), sat);
}

// The three LUTs are the same size (512x512, 64 levels), so the lookup is worked out once and only the texture read
// is picked.
vec3 mellowDecodeLut(vec3 color, vec2 fragCoord, int lut) {
    float texSize = float(textureSize(mellowLutPunchy, 0).x);
    float cellNr = round(pow(texSize, 1.0 / 3.0));
    color += (mellowIgn(fragCoord) - 0.5) / (cellNr * cellNr);
    color = clamp(color, 0.0, 0.999);
    float offsetInCell = fract(color.b * cellNr);
    vec2 cellOffset = vec2(floor(offsetInCell * cellNr), floor(color.b * cellNr));
    vec2 offset = color.rg / cellNr;
    vec2 coord = clamp(offset + cellOffset / cellNr, 0.5 / texSize, 1.0 - 0.5 / texSize);
    if (lut == 1) return texture(mellowLutVintage, coord).rgb;
    if (lut == 2) return texture(mellowLutMellow, coord).rgb;
    return texture(mellowLutPunchy, coord).rgb;
}

void hookPostGrade(inout vec3 color, vec2 uv) {
    vec2 fragCoord = uv * vec2(textureSize(hookPostGradeInput, 0));
    vec3 c = clamp(color, 0.0, 1.0);
    int lut = int(MELLOW_LUT);
    if (lut != 0) { c = mellowDecodeLut(c, fragCoord, lut); }
    c = mellowApplyVibrance(c, float(MELLOW_VIBRANCE));
    c = mix(vec3(mellowGradeLuminance(c)), c, float(MELLOW_SATURATION));
    c = (c - 0.5) * float(MELLOW_CONTRAST) + 0.5;
    // Mellow's last step: a dither of about two 8-bit steps, against banding in the sky.
    c += (mellowIgn(fragCoord) - 0.5) / 128.0;
    color = clamp(c, 0.0, 1.0);
}

#endif
