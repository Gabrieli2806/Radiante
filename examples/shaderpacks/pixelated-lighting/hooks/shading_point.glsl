#ifndef RADIANTE_HOOK_SHADING_POINT_GLSL
#define RADIANTE_HOOK_SHADING_POINT_GLSL

// Pixelated Lighting: hookSurface (hooks/surface.glsl) shades every hit at the centre of its texel. Leaving
// RADIANTE_HOOK_SHADING_POINT_DEFAULT out of this file has radiante-pt start the camera's reflections off water, glass
// and mirrors from that centre too (the offset comes back in the fourth layer of material_state, which configs.json
// gives "depth": 4), so a near reflection is one solid square per texel like a far one.
//
// PIXEL_LIGHT_COHERENT: every pixel of a texel also takes the same random numbers. hookSurface reseeds the path from
// the texel the camera sees, and hookSampleKey below hands the low-discrepancy samples the texel's key instead of the
// pixel's, so the pixels of a texel trace the same shadow ray, the same bounce and the same light sample, and the
// texel comes out as one flat value each frame instead of a grain the denoiser smooths across its edges. Deeper
// hits key on the path's seed, which is already the same for the whole texel.

#ifndef PIXEL_LIGHT_COHERENT
#    define PIXEL_LIGHT_COHERENT 1
#endif

// Set by hookSurface for the hit being shaded; false until then, and for a glowing texel it leaves alone.
bool g_pixelLightKeyed = false;
uint g_pixelLightKey = 0u;

uint hookSampleKey(MainRay ray, uint pixelKey) {
    return g_pixelLightKeyed ? g_pixelLightKey : pixelKey;
}

#endif
