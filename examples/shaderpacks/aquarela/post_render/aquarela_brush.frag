#version 460
#extension GL_GOOGLE_include_directive : require

// Aquarela, second post pass: the brush. The finished picture (after tone mapping, before the HUD) goes through an
// anisotropic Kuwahara filter with polynomial sector weights (Kyprianidis, Semmo, Kang and Doellner, "Anisotropic
// Kuwahara Filtering with Polynomial Weighting Functions", 2010). Round each pixel, an ellipse stretched along the
// local edge (aquarela_flow, from aquarela_structure.frag) is cut into eight overlapping sectors; each sector's mean
// colour counts by how flat it is, so a pixel takes the colour of the side of an edge it lies on, and texture noise
// melts into flat strokes that follow the shapes. That is the flat wash of a brush, and it keeps edges crisp for the
// edge darkening that follows (aquarela_paper.frag).
//
// Before the filter the picture is read through a slow wobble, a displacement by low-frequency noise fixed to the
// screen, so straight block edges come out as the slightly wavering lines of a hand.
//
// Cost: about pi * radius^2 reads per pixel (50 at the default radius of 4), whatever the anisotropy, since the
// ellipse keeps the circle's area. The radius is in pixels of the picture.

#include "common/pack_post.glsl"
#include "aquarela/aquarela_noise.glsl"

#ifndef AQUARELA_BRUSH
#    define AQUARELA_BRUSH 4
#endif
#ifndef AQUARELA_WOBBLE
#    define AQUARELA_WOBBLE 0.5
#endif

// out:ldr is read directly (set 3, binding 0): this pass writes aquarela_wash, not out:ldr. The flow field is a pack
// image, in set 4 at its sampled_binding.
layout(set = 3, binding = 0) uniform sampler2D aquarelaPicture;
layout(set = 4, binding = 60) uniform sampler2D aquarelaFlow;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outWash;

// The sectors' weighting polynomials, in the filter's coordinates (the ellipse mapped to a disc of radius 0.5).
// ZETA is how much the sectors overlap at the centre; ETA bends each sector's edge so it reaches zero about 34
// degrees off its axis at the rim, overlapping its neighbours (45 degrees apart) by a third.
const float ZETA = 0.08;
const float ETA = 6.4;
// How much a sector's variance counts against it (the Kuwahara's sharpness, with q = 8 below): higher keeps edges
// harder, lower blends sectors into a soft average.
const float SHARPNESS = 255.0;
// How far the ellipse may stretch along an edge (Kyprianidis's alpha): 1 makes it up to twice as long as wide.
const float ALPHA = 1.0;

vec3 aquarelaRead(vec2 uv) {
    return clamp(texture(aquarelaPicture, uv).rgb, 0.0, 1.0);
}

void aquarelaWeights(vec2 v, out float w[8]) {
    float sum = 0.0;
    float vxx = ZETA - ETA * v.x * v.x;
    float vyy = ZETA - ETA * v.y * v.y;
    float z;
    z = max(0.0, v.y + vxx);
    w[0] = z * z;
    z = max(0.0, -v.x + vyy);
    w[2] = z * z;
    z = max(0.0, -v.y + vxx);
    w[4] = z * z;
    z = max(0.0, v.x + vyy);
    w[6] = z * z;
    // The odd sectors: the same polynomials on v turned by 45 degrees.
    vec2 d = 0.70710678 * vec2(v.x - v.y, v.x + v.y);
    vxx = ZETA - ETA * d.x * d.x;
    vyy = ZETA - ETA * d.y * d.y;
    z = max(0.0, d.y + vxx);
    w[1] = z * z;
    z = max(0.0, -d.x + vyy);
    w[3] = z * z;
    z = max(0.0, -d.y + vxx);
    w[5] = z * z;
    z = max(0.0, d.x + vyy);
    w[7] = z * z;
    for (int k = 0; k < 8; k++) { sum += w[k]; }
    // Normalised into a partition of unity, under a Gaussian falling to 0.46 at the rim.
    float g = exp(-3.125 * dot(v, v)) / max(sum, 1e-8);
    for (int k = 0; k < 8; k++) { w[k] *= g; }
}

vec3 aquarelaKuwahara(vec2 uv, vec2 texel, float radius) {
    // The edge's direction and how marked it is, from the structure tensor (E, F, G), read with a little more
    // smoothing than the bilinear read alone gives.
    vec2 flowTexel = 1.0 / vec2(textureSize(aquarelaFlow, 0));
    vec3 tensor = 0.4 * texture(aquarelaFlow, uv).xyz;
    tensor += 0.15 * texture(aquarelaFlow, uv + vec2(flowTexel.x, flowTexel.y)).xyz;
    tensor += 0.15 * texture(aquarelaFlow, uv + vec2(-flowTexel.x, flowTexel.y)).xyz;
    tensor += 0.15 * texture(aquarelaFlow, uv + vec2(flowTexel.x, -flowTexel.y)).xyz;
    tensor += 0.15 * texture(aquarelaFlow, uv + vec2(-flowTexel.x, -flowTexel.y)).xyz;
    float e = tensor.x;
    float f = tensor.y;
    float g = tensor.z;
    float root = sqrt(max((e - g) * (e - g) + 4.0 * f * f, 0.0));
    float major = 0.5 * (e + g + root);
    float minor = 0.5 * (e + g - root);
    // Along the edge: the eigenvector of the smaller eigenvalue. Where the picture is flat any direction will do.
    vec2 along = vec2(major - e, -f);
    along = dot(along, along) > 1e-12 ? normalize(along) : vec2(1.0, 0.0);
    vec2 across = vec2(-along.y, along.x);
    float anisotropy = major + minor > 1e-8 ? (major - minor) / (major + minor) : 0.0;

    float a = radius * clamp((ALPHA + anisotropy) / ALPHA, 0.1, 2.0);
    float b = radius * clamp(ALPHA / (ALPHA + anisotropy), 0.1, 2.0);
    int maxX = int(sqrt(a * a * along.x * along.x + b * b * along.y * along.y));
    int maxY = int(sqrt(a * a * along.y * along.y + b * b * along.x * along.x));
    vec2 scale = vec2(0.5 / a, 0.5 / b);

    vec4 m[8];
    vec3 s[8];
    // The centre pixel: every sector's weight is the same there, an eighth.
    vec3 centre = aquarelaRead(uv);
    for (int k = 0; k < 8; k++) {
        m[k] = vec4(centre * 0.125, 0.125);
        s[k] = centre * centre * 0.125;
    }
    // Half the ellipse; each offset also stands for its mirror, which falls in the opposite sector.
    for (int j = 0; j <= maxY; j++) {
        for (int i = -maxX; i <= maxX; i++) {
            if (j == 0 && i <= 0) { continue; }
            vec2 offset = vec2(float(i), float(j));
            vec2 v = vec2(dot(offset, along), dot(offset, across)) * scale;
            if (dot(v, v) > 0.25) { continue; }
            vec3 c0 = aquarelaRead(uv + offset * texel);
            vec3 c1 = aquarelaRead(uv - offset * texel);
            float w[8];
            aquarelaWeights(v, w);
            for (int k = 0; k < 8; k++) {
                m[k] += vec4(c0 * w[k], w[k]);
                s[k] += c0 * c0 * w[k];
                m[(k + 4) & 7] += vec4(c1 * w[k], w[k]);
                s[(k + 4) & 7] += c1 * c1 * w[k];
            }
        }
    }

    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int k = 0; k < 8; k++) {
        vec3 mean = m[k].rgb / m[k].w;
        vec3 variance = abs(s[k] / m[k].w - mean * mean);
        // 1 / (1 + (sharpness * sigma^2)^(q / 2)) with q = 8, clamped so the fourth power stays in range.
        float x = min(SHARPNESS * (variance.r + variance.g + variance.b), 1e4);
        float x2 = x * x;
        float weight = 1.0 / (1.0 + x2 * x2);
        sum += mean * weight;
        total += weight;
    }
    return sum / max(total, 1e-20);
}

void main() {
    vec2 size = vec2(textureSize(aquarelaPicture, 0));
    vec2 texel = 1.0 / size;
    vec2 uv = fragTexCoord;

    // The wobble: two octaves of value noise, fixed to the screen and sized to its height, so it looks the same at
    // any resolution; up to about three pixels each way at 1080p with the setting at 1.
    float wobble = clamp(float(AQUARELA_WOBBLE), 0.0, 1.0);
    if (wobble > 0.0) {
        float scale = size.y / 1080.0;
        vec2 pixel = uv * size;
        vec2 slow = pixel / (scale * 110.0);
        vec2 quick = pixel / (scale * 37.0);
        vec2 shift = vec2(aquarelaValueNoise(slow, 11u), aquarelaValueNoise(slow, 23u)) - 0.5;
        shift += 0.45 * (vec2(aquarelaValueNoise(quick, 37u), aquarelaValueNoise(quick, 41u)) - 0.5);
        uv += shift * (wobble * 6.0 * scale) * texel;
    }

    float radius = float(int(AQUARELA_BRUSH));
    vec3 colour = radius >= 1.0 ? aquarelaKuwahara(uv, texel, radius) : aquarelaRead(uv);
    outWash = vec4(colour, texture(aquarelaPicture, fragTexCoord).a);
}
