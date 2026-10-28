#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// Pixelated Lighting: an optional retro palette over the finished picture, off by default (the pack's own look keeps
// natural colours). Each channel is snapped to PIXEL_LIGHT_PALETTE levels (0 keeps the colours as they are), with a
// 4x4 ordered dither between neighbouring levels so gradients become the checkered steps of old games rather than
// bands. The dither is fixed to the screen's pixels, so it does not crawl;
// PIXEL_LIGHT_DITHER sets how much of it there is. Both are live settings ("runtime": true): the defines read the
// world uniform (post_hook_grade.frag declares it), so they are tested with if, not #if.
#ifndef PIXEL_LIGHT_PALETTE
#    define PIXEL_LIGHT_PALETTE 0
#endif
#ifndef PIXEL_LIGHT_DITHER
#    define PIXEL_LIGHT_DITHER 0.6
#endif

void hookPostGrade(inout vec3 color, vec2 uv) {
    int palette = int(PIXEL_LIGHT_PALETTE);
    if (palette <= 1) { return; }
    float levels = float(palette - 1);
    const float bayer[16] = float[](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0,
                                    5.0);
    ivec2 pixel = ivec2(gl_FragCoord.xy) & 3;
    float threshold = (bayer[pixel.y * 4 + pixel.x] + 0.5) / 16.0 - 0.5;
    vec3 c = clamp(color, 0.0, 1.0);
    c = floor(c * levels + 0.5 + threshold * float(PIXEL_LIGHT_DITHER)) / levels;
    color = clamp(c, 0.0, 1.0);
}

#endif
