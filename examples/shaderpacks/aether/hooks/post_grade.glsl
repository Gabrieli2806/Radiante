#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// Aether: the lens and the film. Over the finished picture, after Radiante's tone mapping (its Bedrock-style curve),
// so on display colours, 0 to 1:
//   Lens Fringes   red and blue drift apart towards the corners, as a real lens's do; the centre stays sharp
//   Glow           Radiante's bloom pyramid (RADIANTE_POST_BLOOM in configs.json) added as light: a gentle glow round
//                  the sun, lamps and bright sky
//   Anamorphic     a soft horizontal streak, blue-tinted, from only the brightest things (the sun, a lamp at night):
//   Streak         the bloom read along the row on both sides
//   Film Grade     warm lifted highlights, slightly teal shadows, a soft S curve with a lifted toe, a little more life
//                  in muted colours
//   Vignette       the corners darken gently
//   Film Grain     a fine grain that changes every frame, stronger in the shadows, as film's is
//   Wide Screen    black bars above and below, up to a cinema's 2.39:1 frame
//   Rush           at a sprint the edges of the picture streak outwards a little, as speed does to the eye
//   Vitals         the picture answers to the player (common/pack_world.glsl's radianteHurt and radianteHealth): a
//                  red pulse from the edges when hit, and with under a third of the health left the colour drains
//                  and the corners close in
// then a dither of half an 8-bit step against banding in the soft sky. All are live settings, read from the world
// uniform while the pass runs (tested with if, never #if). The pass runs because configs.json sets
// RADIANTE_HOOK_POST_GRADE; hooks/underwater.glsl runs in it first.
#include "aether/aether_common.glsl"
// Radiante's own noise (the public radiante/v1 header): the grain's per-pixel, per-frame hash.
#include <radiante/v1/noise.glsl>

// The bloom read along the picture's row on both sides, only what is well over white: a streak from the sun or a
// lamp, nothing from the sky. Linear light, added to the picture.
vec3 aetherStreak(vec2 uv) {
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int i = 1; i <= 8; i++) {
        float x = float(i) / 8.0;
        float reach = x * x * 0.30;
        float weight = exp(-x * 3.0);
        vec2 right = vec2(uv.x + reach, uv.y);
        vec2 left = vec2(uv.x - reach, uv.y);
        if (right.x <= 1.0) { sum += max(radianteBloom(right) - 1.2, vec3(0.0)) * weight; }
        if (left.x >= 0.0) { sum += max(radianteBloom(left) - 1.2, vec3(0.0)) * weight; }
        total += 2.0 * weight;
    }
    vec3 streak = sum / total;
    streak = streak / (1.0 + streak);
    return streak * vec3(0.55, 0.75, 1.0) * 0.8;
}

vec3 aetherFilmGrade(vec3 c) {
    float luma = aetherLuma(c);
    // Split toning: slightly teal shadows, warm highlights by default (the Shadow Tone and Highlight Tone settings).
    c *= mix(AETHER_SHADOW_TONE, AETHER_HIGHLIGHT_TONE, smoothstep(0.1, 0.75, luma));
    // A soft S curve: deeper mids, the highlights rolled off rather than clipped.
    c = clamp(c, 0.0, 1.0);
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.22);
    // A film toe: the blacks lifted a touch, towards teal.
    c += vec3(0.010, 0.018, 0.022) * (1.0 - c);
    // Vibrance: muted colours a little livelier, strong ones (Minecraft's greens) left where they are.
    float spread = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    c = mix(vec3(aetherLuma(c)), c, 1.0 + 0.12 * (1.0 - clamp(spread * 2.0, 0.0, 1.0)));
    return clamp(c, 0.0, 1.0);
}

void hookPostGrade(inout vec3 color, vec2 uv) {
    float fringes = clamp(float(AETHER_ABERRATION), 0.0, 1.0);
    // Not under water: the fringes read the picture as it was, which would undo hookUnderwater's waver in red and
    // blue.
    if (fringes > 0.0 && skyUBO.cameraSubmersionType != 1u) {
        vec2 fromCentre = uv - 0.5;
        vec2 shift = fromCentre * dot(fromCentre, fromCentre) * (0.012 * fringes);
        color.r = texture(hookPostGradeInput, clamp(uv + shift, vec2(0.0), vec2(1.0))).r;
        color.b = texture(hookPostGradeInput, clamp(uv - shift, vec2(0.0), vec2(1.0))).b;
    }

    float glow = clamp(float(AETHER_BLOOM), 0.0, 1.0);
    float streak = clamp(float(AETHER_STREAK), 0.0, 1.0);
    if (glow > 0.0 || streak > 0.0) {
        // Added as light: decoded, added, encoded again.
        vec3 linear = pow(max(color, vec3(0.0)), vec3(2.2));
        if (glow > 0.0) {
            vec3 bloom = radianteBloom(uv) * (0.6 * glow);
            linear += bloom / (1.0 + bloom);
        }
        if (streak > 0.0) { linear += aetherStreak(uv) * streak; }
        color = pow(linear, vec3(1.0 / 2.2));
    }

    float grade = clamp(float(AETHER_GRADE), 0.0, 1.0);
    if (grade > 0.0) { color = mix(clamp(color, 0.0, 1.0), aetherFilmGrade(color), grade); }

    float vignette = clamp(float(AETHER_VIGNETTE), 0.0, 1.0);
    if (vignette > 0.0) {
        // 1 at the corners.
        float corner = length(uv - 0.5) * 1.41421356;
        color *= 1.0 - vignette * 0.55 * smoothstep(0.25, 1.0, corner) * corner;
    }

    // Rush: the picture read a few times along the line from the centre, more towards the edges, by how much the
    // player sprints (radianteSprinting, eased) and how fast they really go. Not under water, where the waver reads
    // the picture its own way.
    float rush = clamp(float(AETHER_RUSH), 0.0, 1.0) * radianteSprinting() * smoothstep(4.5, 7.0, radianteSpeed());
    if (rush > 0.001 && skyUBO.cameraSubmersionType != 1u) {
        vec2 fromCentre = uv - 0.5;
        float reach = 0.035 * rush * dot(fromCentre, fromCentre) * 4.0;
        vec3 sum = color;
        for (int i = 1; i <= 4; i++) {
            sum += texture(hookPostGradeInput, clamp(uv - fromCentre * reach * float(i), vec2(0.0), vec2(1.0))).rgb;
        }
        color = mix(color, sum / 5.0, smoothstep(0.1, 0.6, length(fromCentre)));
    }

    float vitals = clamp(float(AETHER_VITALS), 0.0, 1.0);
    if (vitals > 0.0) {
        float corner = length(uv - 0.5) * 1.41421356;
        // Running out of air: from a third of the breath down the edges close in, dark blue, pulsing faster.
        float breathless = (1.0 - smoothstep(0.0, 0.33, radianteAir())) * vitals;
        if (breathless > 0.0) {
            float pulse = 0.75 + 0.25 * sin(radianteTime() * mix(3.0, 9.0, breathless));
            color = mix(color, vec3(0.01, 0.03, 0.09), 0.7 * breathless * pulse * smoothstep(0.2, 1.0, corner));
        }
        // Little health: 0 from a third of it up, 1 with none left.
        float faint = (1.0 - smoothstep(0.0, 0.33, radianteHealth())) * vitals;
        if (faint > 0.0) {
            color = mix(color, vec3(aetherLuma(color)), 0.65 * faint);
            color *= 1.0 - 0.5 * faint * smoothstep(0.3, 1.0, corner);
        }
        // A hit: red in from the edges, gone in about half a second.
        float hit = radianteHurt() * vitals;
        if (hit > 0.0) {
            float edge = smoothstep(0.25, 1.0, corner);
            color = mix(color, vec3(0.75, 0.04, 0.03), 0.55 * hit * edge);
        }
    }

    vec2 size = vec2(textureSize(hookPostGradeInput, 0));
    vec2 pixel = uv * size;

    float grain = clamp(float(AETHER_GRAIN), 0.0, 1.0);
    if (grain > 0.0) {
        // Two hashes for a grain with a soft middle rather than flat noise; less of it in the highlights.
        float a = radianteHash(uvec2(pixel), radianteFrame());
        float b = radianteHash(uvec2(pixel) + uvec2(71u, 13u), radianteFrame());
        color += (a + b - 1.0) * (0.05 * grain) * (1.0 - 0.6 * aetherLuma(clamp(color, 0.0, 1.0)));
    }

    float dither = fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y));
    color = clamp(color + (dither - 0.5) / 510.0, 0.0, 1.0);

    float letterbox = clamp(float(AETHER_LETTERBOX), 0.0, 1.0);
    if (letterbox > 0.0) {
        // The frame goes from the screen's own shape (0) to 2.39:1 (1); on a screen already that wide, no bars.
        float screen = size.x / max(size.y, 1.0);
        float frame = max(mix(screen, 2.39, letterbox), screen);
        float bar = 0.5 * (1.0 - screen / frame);
        if (uv.y < bar || uv.y > 1.0 - bar) { color = vec3(0.0); }
    }
}

#endif
