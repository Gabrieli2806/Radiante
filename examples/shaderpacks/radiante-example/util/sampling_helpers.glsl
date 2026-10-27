#ifndef SAMPLING_HELPERS_GLSL
#define SAMPLING_HELPERS_GLSL

// Manual texture filtering for atlas sprites. Hardware filtering would blend in texels of the neighbouring sprite at
// a tile's border, so the PBR maps are read texel by texel here, with every read kept inside the sprite's own rect.

#include "disney.glsl"
#include "color_space.glsl"

// Keeps a texel coordinate inside a texture of the given size (and never negative, even for an empty one).
ivec2 clampTexelCoord(ivec2 coord, ivec2 size) {
    return clamp(coord, ivec2(0), max(size - ivec2(1), ivec2(0)));
}

// Clamps a UV to a sprite rect pulled in by half a texel on every side, so a bilinear footprint centred there never
// reaches past the rect. The corners may come in either order; a rect thinner than one texel collapses to its
// lower edge.
vec2 clampUvToRect(vec2 uv, vec2 uvMin, vec2 uvMax, ivec2 size) {
    vec2 rectLo = min(uvMin, uvMax);
    vec2 rectHi = max(uvMin, uvMax);
    vec2 inset = 0.5 / vec2(size);

    rectLo += inset;
    rectHi -= inset;
    rectHi = max(rectHi, rectLo);

    return clamp(uv, rectLo, rectHi);
}

// Point sampling: the texel the UV falls in.
vec4 sampleNearest(sampler2D tex, vec2 uv, int lod, bool isSRGB) {
    ivec2 extent = textureSize(tex, lod);
    if (extent.x <= 0 || extent.y <= 0) { return vec4(0.0); }
    ivec2 cell = ivec2(uv * vec2(extent));
    return sampleTexture(tex, clampTexelCoord(cell, extent), lod, isSRGB);
}

// Bilinear filtering done by hand from four texel fetches, each clamped to the texture.
vec4 sampleBilinear(sampler2D tex, vec2 uv, int lod, bool isSRGB) {
    ivec2 extent = textureSize(tex, lod);
    if (extent.x <= 0 || extent.y <= 0) { return vec4(0.0); }

    // Texel centres sit at half-integers, so shift by half a texel to find the lower-left one of the four.
    vec2 texelPos = uv * vec2(extent) - vec2(0.5);
    ivec2 lo = ivec2(floor(texelPos));
    ivec2 hi = lo + ivec2(1);
    vec2 weight = fract(texelPos);

    ivec2 atLL = clampTexelCoord(ivec2(lo.x, lo.y), extent);
    ivec2 atHL = clampTexelCoord(ivec2(hi.x, lo.y), extent);
    ivec2 atLH = clampTexelCoord(ivec2(lo.x, hi.y), extent);
    ivec2 atHH = clampTexelCoord(ivec2(hi.x, hi.y), extent);

    vec4 valueLL = sampleTexture(tex, atLL, lod, isSRGB);
    vec4 valueHL = sampleTexture(tex, atHL, lod, isSRGB);
    vec4 valueLH = sampleTexture(tex, atLH, lod, isSRGB);
    vec4 valueHH = sampleTexture(tex, atHH, lod, isSRGB);

    vec4 rowLow = mix(valueLL, valueHL, weight.x);
    vec4 rowHigh = mix(valueLH, valueHH, weight.x);
    return mix(rowLow, rowHigh, weight.y);
}

// Reads a PBR map (normal or specular) of an atlas sprite at a fractional mip level. The level is rounded down and
// kept within the mip chain; samplingMode 0 is point sampling (the vanilla pixel look), anything else bilinear.
vec4 samplePBRTexture(sampler2D tex,
                      vec2 uv,
                      vec2 atlasUvMin,
                      vec2 atlasUvMax,
                      float lod,
                      uint samplingMode) {
    int level = clamp(int(floor(lod)), 0, max(textureQueryLevels(tex) - 1, 0));
    ivec2 extent = textureSize(tex, level);
    if (extent.x <= 0 || extent.y <= 0) { return vec4(0.0); }

    vec2 insideUv = clampUvToRect(uv, atlasUvMin, atlasUvMax, extent);
    if (samplingMode == 0u) {
        return sampleNearest(tex, insideUv, level, false);
    } else {
        return sampleBilinear(tex, insideUv, level, false);
    }
}

#endif
