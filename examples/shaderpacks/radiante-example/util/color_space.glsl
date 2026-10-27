#ifndef COLOR_SPACE_GLSL
#define COLOR_SPACE_GLSL

// sRGB decoding and texture reads that optionally decode. Textures uploaded as UNORM hold sRGB-encoded values; the
// lighting needs linear ones.

// The exact piecewise sRGB transfer curve, inverted: a straight segment near black, a 2.4 power above it.
vec3 srgbToLinear(vec3 srgbColor) {
    vec3 linearSegment = srgbColor / 12.92;
    vec3 powerSegment = pow((srgbColor + 0.055) / 1.055, vec3(2.4));
    bvec3 inLinearPart = lessThanEqual(srgbColor, vec3(0.04045));
    return mix(powerSegment, linearSegment, inLinearPart);
}

// Alpha is stored linearly and passes through.
vec4 srgbToLinear(vec4 srgbColor) {
    return vec4(srgbToLinear(srgbColor.rgb), srgbColor.a);
}

vec4 applySrgbToLinear(vec4 sampledColor, bool isSRGB) {
    if (isSRGB) { return srgbToLinear(sampledColor); }
    return sampledColor;
}

// The same decode wrapped around each kind of read the shaders use.

vec4 sampleTexture(sampler2D tex, vec2 uv, bool isSRGB) {
    return applySrgbToLinear(texture(tex, uv), isSRGB);
}

vec4 sampleTexture(sampler2D tex, vec2 uv, float lod, bool isSRGB) {
    return applySrgbToLinear(textureLod(tex, uv, lod), isSRGB);
}

vec4 sampleTexture(sampler2D tex, ivec2 coord, int lod, bool isSRGB) {
    return applySrgbToLinear(texelFetch(tex, coord, lod), isSRGB);
}

vec4 sampleTexture(samplerCube tex, vec3 dir, bool isSRGB) {
    return applySrgbToLinear(texture(tex, dir), isSRGB);
}

#endif
