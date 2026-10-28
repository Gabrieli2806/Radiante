#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// Pixel Light: every point a ray hits is moved to the centre of its texture texel before it is lit, and is seen from
// there. Shadow rays, block light samples, bounce rays and reflection rays all start from that centre, and the view
// the surface's reflections and highlights are worked out with points at it, so a texel gets one light, one shadow
// and one reflection: shadow edges, torch falloff, bounce light, reflections and highlights come out in the texture's
// own pixels. Done on every bounce, so what a reflection or a bounce carries is pixelated too.
//
// The texel is the texture's own (hookTexelCentre), not a grid in the world: on cross plants, vines and other quads
// that do not sit on the block grid, a world grid cut texels in two and drew dark seams between them.
//
// PIXEL_LIGHT_TEXELS: 0 turns it off; above 0, the texture's texels are merged so a block has at most that many per
// side (16 is the texture's own pixels for vanilla). PIXEL_LIGHT_FLAT drops the normal map, which would shade inside a
// texel and undo the steps.
#ifndef PIXEL_LIGHT_TEXELS
#    define PIXEL_LIGHT_TEXELS 16
#endif
#ifndef PIXEL_LIGHT_FLAT
#    define PIXEL_LIGHT_FLAT 1
#endif

void hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce) {
#if PIXEL_LIGHT_FLAT != 0
    surface.shadingNormal = surface.geometricNormal;
#endif
#if PIXEL_LIGHT_TEXELS > 0
    // Glowing texels keep their position: their light is the light list's, not this point's.
    if (surface.mat.emission > 0.0) { return; }
    vec3 centre = hookTexelCentre(surface.uv, surface.worldPos);
    // Coarser than the texture: a texel bigger than 1/PIXEL_LIGHT_TEXELS of a block is kept, smaller ones are merged
    // by snapping the centre once more on the block grid, along the face.
    const float grid = float(PIXEL_LIGHT_TEXELS);
    if (grid < 16.0) {
        vec3 cell = vec3(worldUBO.cameraPos.xyz - floor(worldUBO.cameraPos.xyz));
        vec3 normal = surface.geometricNormal;
        vec3 local = centre + cell - normal * (0.25 / grid);
        vec3 snapped = (floor(local * grid) + 0.5) / grid;
        snapped += normal * dot(centre + cell - snapped, normal);
        centre = snapped - cell;
    }
    surface.worldPos = centre;
    g_hookViewDir = normalize(g_hit.rayOrigin - centre);
#endif
}

#endif
