#ifndef ALPHA_MODE_GLSL
#define ALPHA_MODE_GLSL

// How a surface's texture alpha is interpreted. The value lives in the same per-geometry field as the text modes
// (text_mode.glsl, 1-8), which is why the modes added later start at 9.

const uint ALPHA_MODE_OPAQUE = 0u;
const uint ALPHA_MODE_CUTOUT = 1u;
const uint ALPHA_MODE_TRANSPARENT = 2u;
// Thin see-through geometry that has to stay visible, such as rain and snow. The any-hit shader keeps a hit with a
// probability equal to alpha, and a kept hit is then shaded like an opaque surface; treated as ordinary
// transparency it would be shaded as clear glass and disappear.
const uint ALPHA_MODE_STOCHASTIC = 9u;
// Multiplicative decals (the block-breaking cracks). Vanilla blends them as dst * src * 2: mid grey leaves the block
// alone and only darker texels darken it. The any-hit shader keeps a hit with probability alpha * (1 - 2 * luma),
// and a kept hit is shaded opaque in the texel's own dark colour.
const uint ALPHA_MODE_DECAL = 10u;

// Additive layers (vanilla's glowing eyes on endermen, spiders and phantoms). Added on top of what is behind, so
// black texels have no effect. The any-hit shader keeps only texels that carry some colour; a kept hit is shaded
// opaque and casts no shadow.
const uint ALPHA_MODE_ADDITIVE = 11u;

// Cutout surfaces (leaves, grass) are solid where alpha reaches this and absent elsewhere.
const float CUTOUT_ALPHA_THRESHOLD = 0.5;

// The coverage a surface is shaded with. Opaque is always fully covered; the hit-or-miss modes (stochastic, decal,
// additive) were already decided in the any-hit shader, so anything barely visible that got through is treated as
// a full hit; cutout snaps at the threshold; plain transparency keeps its alpha.
float resolveSurfaceAlpha(float alpha, uint alphaMode) {
    alpha = clamp(alpha, 0.0, 1.0);

    if (alphaMode == ALPHA_MODE_OPAQUE) { return 1.0; }

    if (alphaMode == ALPHA_MODE_STOCHASTIC || alphaMode == ALPHA_MODE_DECAL || alphaMode == ALPHA_MODE_ADDITIVE) {
        return alpha >= 0.05 ? 1.0 : 0.0;
    }

    if (alphaMode == ALPHA_MODE_CUTOUT) {
        return alpha >= CUTOUT_ALPHA_THRESHOLD ? 1.0 : 0.0;
    }

    return alpha;
}

#endif
