#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// Neon Dusk: bloom with colour fringes and a teal and magenta grade over the finished picture. Turned on by
// RADIANTE_HOOK_POST_GRADE in configs.json; RADIANTE_POST_BLOOM there has Radiante draw its bloom pyramid, which
// common/pack_post.glsl gives this hook (radianteBloom). Radiante's own tone mapping has run already: the colour here
// is a display colour, 0 to 1. A clean picture: no scanlines, grain or noise, only a dither too small to see, against
// banding in the dark purple gradients.
//
// All the strengths are live settings: their defines read the world uniform, so they are tested with if, not #if.
#ifndef NEON_BLOOM
#    define NEON_BLOOM 0.3
#endif
#ifndef NEON_FRINGE
#    define NEON_FRINGE 0.5
#endif
#ifndef NEON_GRADE
#    define NEON_GRADE 0.8
#endif
#ifndef NEON_SATURATION
#    define NEON_SATURATION 1.15
#endif

const vec3 NEON_POST_LUMA = vec3(0.2126, 0.7152, 0.0722);

// The bloom, its red read a little outwards and its blue a little inwards (and both a touch sideways), so every
// bright light gets a magenta rim on one side and a cyan one on the other, more towards the edges of the screen.
vec3 neonFringedBloom(vec2 uv, float fringe) {
    if (fringe <= 0.0) { return radianteBloom(uv); }
    vec2 offset = ((uv - 0.5) * 0.02 + vec2(0.003, 0.0)) * fringe;
    return vec3(radianteBloom(clamp(uv + offset, 0.0, 1.0)).r, radianteBloom(uv).g,
                radianteBloom(clamp(uv - offset, 0.0, 1.0)).b);
}

// Interleaved gradient noise, for the dither.
float neonIgn(vec2 pixel) {
    return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y));
}

void hookPostGrade(inout vec3 color, vec2 uv) {
    float fringe = max(float(NEON_FRINGE), 0.0);
    if (fringe > 0.0) {
        // The picture's own fringes, towards the corners only: red outwards, blue inwards.
        vec2 fromCentre = uv - 0.5;
        vec2 shift = fromCentre * dot(fromCentre, fromCentre) * 0.012 * fringe;
        color.r = texture(hookPostGradeInput, uv + shift).r;
        color.b = texture(hookPostGradeInput, uv - shift).b;
    }

    float bloom = max(float(NEON_BLOOM), 0.0);
    if (bloom > 0.0) {
        // As radianteAddBloom does: decoded to linear, the bloom added as light, encoded again.
        vec3 linear = pow(max(color, vec3(0.0)), vec3(2.2));
        vec3 glow = neonFringedBloom(uv, fringe) * bloom;
        color = pow(linear + glow / (1.0 + glow), vec3(1.0 / 2.2));
    }

    vec3 c = clamp(color, 0.0, 1.0);
    float grade = clamp(float(NEON_GRADE), 0.0, 1.0);
    if (grade > 0.0) {
        float luma = dot(c, NEON_POST_LUMA);
        // Split toning: shadows towards teal, highlights towards magenta, the brightness kept.
        vec3 tinted = c * mix(vec3(0.84, 1.03, 1.12), vec3(1.12, 0.9, 1.08), smoothstep(0.08, 0.7, luma));
        tinted *= luma / max(dot(tinted, NEON_POST_LUMA), 1e-5);
        // A gentle S curve about the middle, and the corners a little darker.
        tinted = mix(tinted, tinted * tinted * (3.0 - 2.0 * tinted), 0.3);
        vec2 fromCentre = uv - 0.5;
        tinted *= 1.0 - 0.15 * smoothstep(0.15, 0.5, dot(fromCentre, fromCentre));
        c = mix(c, clamp(tinted, 0.0, 1.0), grade);
    }
    float saturation = float(NEON_SATURATION);
    c = mix(vec3(dot(c, NEON_POST_LUMA)), c, saturation);

    vec2 pixel = uv * vec2(textureSize(hookPostGradeInput, 0));
    c += (neonIgn(pixel) - 0.5) / 255.0;
    color = clamp(c, 0.0, 1.0);
}

#endif
