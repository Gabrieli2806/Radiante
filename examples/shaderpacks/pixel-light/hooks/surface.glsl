#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// Pixel Light: every point a ray hits is moved to the centre of its texel before it is lit. Shadow rays, block light
// samples, the bounce rays and the reflection rays all start from that centre, so a texel gets one light, one shadow
// and one reflection: shadow edges, torch falloff and bounce light come out in the texture's own pixels instead of
// smooth gradients. Done on every bounce, so light seen in a reflection or carried by a bounce is pixelated too.
// PIXEL_LIGHT_TEXELS is the grid per block (16 matches vanilla textures; 0 turns it off). PIXEL_LIGHT_FLAT drops the
// normal map, which would shade inside a texel and undo the steps.
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
    // Lava and other glowing surfaces keep their position: their light is the light list's, not this point's.
    if (surface.mat.emission > 0.0) { return; }
    const float grid = float(PIXEL_LIGHT_TEXELS);
    // worldPos is relative to the camera. The grid has to stay on the world's blocks, or it would slide as the player
    // walks: the camera's position within its block (exact in doubles) puts it back on them.
    vec3 cell = vec3(worldUBO.cameraPos.xyz - floor(worldUBO.cameraPos.xyz));
    vec3 normal = surface.geometricNormal;
    // A hair inside the surface, so a point exactly on a block face falls in that block's texel and not its neighbour's.
    vec3 local = surface.worldPos + cell - normal * (0.25 / grid);
    vec3 centre = (floor(local * grid) + 0.5) / grid;
    // Back onto the face's plane: only the position along the face is snapped.
    centre += normal * dot(surface.worldPos + cell - centre, normal);
    surface.worldPos = centre - cell;
#endif
}

#endif
