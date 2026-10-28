#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// Radiante Example Pack: lens effects and a colour grade over the finished picture. Turned on by
// RADIANTE_HOOK_POST_GRADE in configs.json; RADIANTE_POST_BLOOM there has Radiante draw its bloom pyramid, which
// post_hook_grade.frag gives this hook through common/pack_post.glsl (radianteBloom).
//
// EXAMPLE_GRADE is compiled in (switching it rebuilds); the strengths are live settings, read from the world uniform
// each frame, so they are tested with if, never #if.
#ifndef EXAMPLE_GRADE_STRENGTH
#    define EXAMPLE_GRADE_STRENGTH 1.0
#endif
#ifndef EXAMPLE_BLOOM
#    define EXAMPLE_BLOOM 0.15
#endif
#ifndef EXAMPLE_LENS
#    define EXAMPLE_LENS 0.0
#endif
// The pack's own debug view (configs.json "debug_views", /radiante debug view example_bloom): 1 while chosen.
#ifndef EXAMPLE_DEBUG_BLOOM
#    define EXAMPLE_DEBUG_BLOOM 0
#endif

// Chromatic aberration: the red and blue channels read a little further out and in from the centre, as a cheap lens
// does at its edges. `amount` 1 shifts the corners by about 0.4% of the screen.
vec3 exampleChromaticAberration(vec3 color, vec2 uv, float amount) {
    vec2 fromCentre = uv - 0.5;
    vec2 shift = fromCentre * dot(fromCentre, fromCentre) * 0.016 * amount;
    color.r = texture(hookPostGradeInput, uv + shift).r;
    color.b = texture(hookPostGradeInput, uv - shift).b;
    return color;
}

// A lens flare: the bloom mirrored through the centre in a few ghosts of different sizes and tints, as the bright
// sun or a lamp reflected between lens elements. Light added to the picture, in linear terms.
vec3 exampleLensFlare(vec2 uv) {
    vec2 mirrored = vec2(1.0) - uv;
    vec2 toCentre = vec2(0.5) - mirrored;
    vec3 flare = vec3(0.0);
    const float ghosts[3] = float[](0.45, 1.1, 1.7);
    const vec3 tints[3] = vec3[](vec3(1.0, 0.6, 0.3), vec3(0.4, 0.8, 1.0), vec3(0.8, 0.4, 1.0));
    for (int i = 0; i < 3; i++) {
        vec2 at = mirrored + toCentre * ghosts[i];
        // Fades out towards the edges, where a real ghost leaves the lens.
        float edge = 1.0 - smoothstep(0.2, 0.7, length(at - 0.5));
        flare += radianteBloom(clamp(at, 0.0, 1.0)) * tints[i] * edge;
    }
    return flare * 0.06;
}

void hookPostGrade(inout vec3 color, vec2 uv) {
    if (EXAMPLE_DEBUG_BLOOM != 0) {
        // What radianteBloom holds, as the screen would show it alone.
        color = radianteBloomDisplay(uv, 1.0);
        return;
    }

    float lens = float(EXAMPLE_LENS);
    if (lens > 0.0) { color = exampleChromaticAberration(color, uv, lens); }

    float bloom = float(EXAMPLE_BLOOM);
    if (bloom > 0.0) { color = radianteAddBloom(color, uv, bloom); }
    if (lens > 0.0) {
        // Added as light: decoded, added, encoded again.
        vec3 linear = pow(max(color, vec3(0.0)), vec3(2.2)) + exampleLensFlare(uv) * lens;
        color = pow(linear, vec3(1.0 / 2.2));
    }

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
    color = mix(clamp(color, 0.0, 1.0), clamp(c, 0.0, 1.0), clamp(float(EXAMPLE_GRADE_STRENGTH), 0.0, 1.0));
#endif
}

#endif
