#ifndef RADIANTE_HOOK_RAIN_SURFACE_GLSL
#define RADIANTE_HOOK_RAIN_SURFACE_GLSL

// Pixelated Lighting: puddles and rain rings in the texture's pixels. The point they are looked up at is snapped to
// the centre of its texel on the block's top (PIXEL_LIGHT_TEXELS across a block, as the shadows and reflections
// are), so each texel is wholly puddle or dry ground and the rings step from texel to texel; the soft rim radiante-pt
// gives a puddle is cut to a hard edge. With PIXEL_LIGHT_TEXELS at 0 they keep radiante-pt's smooth look.
#ifndef PIXEL_LIGHT_TEXELS
#    define PIXEL_LIGHT_TEXELS 16
#endif

vec3 hookRainPosition(vec3 worldPos) {
#if PIXEL_LIGHT_TEXELS > 0
    const float texels = float(PIXEL_LIGHT_TEXELS);
    // worldPos is camera-relative: snap in the world's grid, then hand it back relative to the camera.
    vec2 cameraFraction = vec2(mod(worldUBO.cameraPos.xz, dvec2(1.0)));
    vec2 world = worldPos.xz + cameraFraction;
    worldPos.xz = (floor(world * texels) + 0.5) / texels - cameraFraction;
#endif
    return worldPos;
}

float hookPuddleEdge(float puddle) {
#if PIXEL_LIGHT_TEXELS > 0
    return step(0.5, puddle);
#else
    return puddle;
#endif
}

#endif
