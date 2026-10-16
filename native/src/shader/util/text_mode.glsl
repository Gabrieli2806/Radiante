#ifndef TEXT_MODE_GLSL
#define TEXT_MODE_GLSL

// How a text quad (signs, name tags, the F3 overlay drawn into the world) reads its glyph texture. The value shares
// the alpha-mode field of the geometry, so these numbers (1-8) must stay clear of the alpha modes in alpha_mode.glsl.

const uint POST_TEXT_MODE_BACKGROUND = 1u;
const uint POST_TEXT_MODE_INTENSITY = 2u;
const uint POST_TEXT_MODE_RGBA = 3u;
const uint POST_TEXT_MODE_BACKGROUND_SEE_THROUGH = 4u;
const uint POST_TEXT_MODE_INTENSITY_SEE_THROUGH = 5u;
const uint POST_TEXT_MODE_RGBA_SEE_THROUGH = 6u;
const uint POST_TEXT_MODE_INTENSITY_POLYGON_OFFSET = 7u;
const uint POST_TEXT_MODE_RGBA_POLYGON_OFFSET = 8u;

// The plate behind the text: a flat colour, no texture.
bool isTextBackgroundMode(uint textMode) {
    return textMode == POST_TEXT_MODE_BACKGROUND || textMode == POST_TEXT_MODE_BACKGROUND_SEE_THROUGH;
}

// Glyph atlases that store coverage in the red channel only.
bool isTextIntensityMode(uint textMode) {
    return textMode == POST_TEXT_MODE_INTENSITY || textMode == POST_TEXT_MODE_INTENSITY_SEE_THROUGH ||
           textMode == POST_TEXT_MODE_INTENSITY_POLYGON_OFFSET;
}

// Glyph atlases with full colour and alpha.
bool isTextRgbaMode(uint textMode) {
    return textMode == POST_TEXT_MODE_RGBA || textMode == POST_TEXT_MODE_RGBA_SEE_THROUGH ||
           textMode == POST_TEXT_MODE_RGBA_POLYGON_OFFSET;
}

// The texture colour a text quad should use, given its mode. Not a text mode: the texture if the quad has one,
// otherwise plain white.
vec4 resolveTextTextureColor(vec4 textureColor, bool useTexture, uint textMode) {
    if (isTextBackgroundMode(textMode)) { return vec4(1.0); }
    if (isTextIntensityMode(textMode)) { return textureColor.rrrr; }
    if (isTextRgbaMode(textMode)) { return textureColor; }
    return useTexture ? textureColor : vec4(1.0);
}

// How much of the texel the glyph covers: the resolved alpha times the vertex colour's alpha, kept in 0..1.
float resolveTextCoverage(vec4 textureColor, bool useTexture, float colorLayerAlpha, uint textMode) {
    return clamp(resolveTextTextureColor(textureColor, useTexture, textMode).a * colorLayerAlpha, 0.0, 1.0);
}

#endif
