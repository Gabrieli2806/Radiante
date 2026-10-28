#ifndef RADIANTE_HOOK_SURFACE_GLSL
#define RADIANTE_HOOK_SURFACE_GLSL

// Pixelated Lighting: every point a ray hits is moved to the centre of its texture texel before it is lit.
// Shadow rays, block light samples, bounce rays and reflection rays all start from that centre, and the normal
// is tilted so every pixel of the texel reflects in the same direction, so a texel gets one light, one shadow and one
// reflection: shadow edges, torch falloff, bounce light, reflections and highlights come out in the texture's
// own pixels. Done on every bounce, so what a reflection or a bounce carries is pixelated too.
//
// The texel is the texture's own (hookTexelCentre), not a grid in the world: on cross plants, vines and other quads
// that do not sit on the block grid, a world grid cut texels in two and drew dark seams between them.
//
// PIXEL_LIGHT_TEXELS: 0 turns it off; above 0, the texture's texels are merged so a block has at most that many per
// side (16 is the texture's own pixels for vanilla). PIXEL_LIGHT_FLAT drops the normal map, which would shade inside a
// texel and undo the steps. PIXEL_LIGHT_COHERENT gives every pixel of a texel the same random numbers
// (hooks/shading_point.glsl), so the texel's light is one flat value instead of a grain.
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
    vec3 hitPos = surface.worldPos;
    surface.worldPos = centre;
    if (int(PIXEL_LIGHT_COHERENT) != 0) {
        if (bounce == 0u || rayCameraPath(mainRay)) {
            // The surface the camera sees: the texel's own key, the same from frame to frame so the low-discrepancy
            // samples stay stratified over the frames the denoiser averages; the path's seed from it and the frame.
            // Keyed on the centre in absolute coordinates, in 1/512 of a block (centres of textures up to 256
            // pixels a side are odd multiples of it, far from a rounding edge).
            dvec3 camera = worldUBO.cameraPos.xyz;
            vec3 local = centre + vec3(camera - floor(camera));
            ivec3 cell = ivec3(floor(local * 512.0 + 0.5)) + ivec3(floor(camera)) * 512;
            g_pixelLightKey = xxhash32(uvec3(cell));
            mainRay.seed = xxhash32(uvec3(g_pixelLightKey, worldUBO.seed, 0x51A7E1u));
        } else {
            // Deeper along the path: the seed the ray arrived with is the same for every pixel of the texel it
            // started from.
            g_pixelLightKey = mainRay.seed;
        }
        g_pixelLightKeyed = true;
    }
    // Reflections per texel: the normal is tilted so that the view of this pixel reflects off it in the direction
    // the view of the texel's centre reflects off the true normal. Every pixel of a texel then reflects the same
    // way, wherever the reflection is traced: water and glass seen by the camera are reflected by the ray
    // generation with this normal, not with the surface hook's view, so moving the view (g_hookViewDir) alone left
    // them smooth; hooks/shading_point.glsl has it start them from the centre too. Highlights follow the same
    // direction.
    vec3 incident = normalize(hitPos - g_hit.rayOrigin);
    vec3 centreIncident = normalize(centre - g_hit.rayOrigin);
    vec3 normal = surface.shadingNormal;
    if (dot(incident, normal) > 0.0) { normal = -normal; }
    vec3 reflected = reflect(centreIncident, normal);
    vec3 tilted = reflected - incident;
    if (dot(tilted, tilted) > 1e-8 && dot(reflected, normal) > 0.0) {
        tilted = normalize(tilted);
        // Back to the side the surface's own normal was on.
        surface.shadingNormal = dot(tilted, surface.shadingNormal) >= 0.0 ? tilted : -tilted;
    }
#endif
}

#endif
