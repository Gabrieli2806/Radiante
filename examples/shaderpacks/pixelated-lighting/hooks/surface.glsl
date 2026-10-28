#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// Pixelated Lighting: shadows and reflections on the texture's pixel grid, the light itself left smooth. The surface
// is shaded where it is, with its own normal: the angle to the sun, the falloff of a torch, the bounce light and the
// glow are radiante-pt's, pixel by pixel. Only where the shadow rays and the reflection rays start moves, to the
// centre of the texel the hit lies in (hooks/ray_origins.glsl), so a texel is either in shadow or not, and every pixel
// of it reflects the same thing: shadow edges and reflections come out in solid squares. Done at every hit, so what a
// reflection shows has pixelated shadows too.
//
// The texel is the texture's own (hookTexelCentre), not a grid in the world: on cross plants, vines and other quads
// that do not sit on the block grid, a world grid cut texels in two and drew dark seams between them.
//
// PIXEL_LIGHT_TEXELS: 0 turns it off; above 0, the texture's texels are merged so a block has at most that many per
// side (16 is the texture's own pixels for vanilla).
#ifndef PIXEL_LIGHT_TEXELS
#    define PIXEL_LIGHT_TEXELS 16
#endif

void hookSurface(inout SampledSurface surface, uint surfaceFlags, uint bounce) {
#if PIXEL_LIGHT_TEXELS > 0
    // What the camera's hand holds is lit from a point moved out of the wall it is pushed into (radiante-pt's
    // ptHandLightPos); its rays are left where radiante-pt starts them.
    if (isHeldSurface(surfaceFlags)) { return; }
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
    g_pixelLightOffset = centre - surface.worldPos;
    // Water: its ripples are a normal map finer than its texture, so even from one texel's centre every pixel
    // reflected a little differently and the squares smeared back into a smooth reflection. With pixelated
    // reflections the ripples are read at the texel's centre, so a texel of water is one flat facet and reflects one
    // thing, as in BetterRTX's Pixelated Lighting.
    if (int(PIXEL_LIGHT_REFLECTIONS) != 0 && isWaterSurface(surfaceFlags) && abs(surface.geometricNormal.y) > 0.75) {
        vec3 viewDir = -normalize(g_hit.rayDir);
        float footprint = mainRay.coneWidth + g_hit.hitT * mainRay.coneSpread;
#ifdef RADIANTE_HOOK_WATER_WAVES_DEFAULT
        if (VPT_WATER_SURFACE_MODE == 2u && bedrockWaterAvailable()) {
            vec3 rippled = bedrockWaterNormal(centre + vec3(worldUBO.cameraPos.xyz), surface.geometricNormal,
                                              viewDir, footprint, surface.geometricNormal.y < 0.0);
            // Half the ripples' tilt: at full strength neighbouring facets pointed so differently that the
            // reflection broke into unrelated squares instead of a picture made of squares.
            surface.shadingNormal = normalize(mix(surface.geometricNormal, rippled, 0.5));
        }
#endif
    }
#endif
}

#endif
