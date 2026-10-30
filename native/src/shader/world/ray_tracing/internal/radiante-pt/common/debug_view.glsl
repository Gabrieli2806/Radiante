#ifndef RADIANTE_DEBUG_VIEW_GLSL
#define RADIANTE_DEBUG_VIEW_GLSL

// Radiante PT's debug views: one of the frame's guide images shown as the picture instead of the lit world. The view
// is chosen with /radiante debug view <name>; configs.json lists them under "debug_views", each setting
// RADIANTE_DEBUG_VIEW to its number (0 when none is chosen, so every view's code compiles out). The ray tracing stage
// fills radiante_debug_view at the render resolution (debug/debug_view.comp, and the closest-hit shader for the
// surface flags), the post stage draws it over the picture (post_render/post_debug_view.frag), after the grade, so a
// pack's own look does not tint it.
#define RADIANTE_DEBUG_VIEW_ALBEDO 1
#define RADIANTE_DEBUG_VIEW_NORMAL 2
#define RADIANTE_DEBUG_VIEW_ROUGHNESS 3
#define RADIANTE_DEBUG_VIEW_METALLIC 4
#define RADIANTE_DEBUG_VIEW_EMISSION 5
#define RADIANTE_DEBUG_VIEW_DEPTH 6
#define RADIANTE_DEBUG_VIEW_MOTION 7
#define RADIANTE_DEBUG_VIEW_FLAGS 8
#define RADIANTE_DEBUG_VIEW_ANYHIT 9
#define RADIANTE_DEBUG_VIEW_BOUNCES 10
#define RADIANTE_DEBUG_VIEW_RAIN 11
#define RADIANTE_DEBUG_VIEW_DIRECT 12
#define RADIANTE_DEBUG_VIEW_INDIRECT 13
#define RADIANTE_DEBUG_VIEW_SHARC 14

// The image's bindings: set 5 in the ray tracing stage (storage), set 4 in the post stage (sampled).
#define RADIANTE_DEBUG_VIEW_SAMPLED_BINDING 120
#define RADIANTE_DEBUG_VIEW_STORAGE_BINDING 121

// Linear colour to what the screen shows (out:ldr holds display values).
vec3 radianteDebugDisplay(vec3 linearColor) {
    return pow(clamp(linearColor, 0.0, 1.0), vec3(1.0 / 2.2));
}

// A count per pixel as a display colour (common/debug_count.glsl): black for none, then blue at 1 through cyan, green
// and yellow to red at 16 and over.
vec3 radianteDebugHeat(uint count) {
    if (count == 0u) return vec3(0.0);
    float t = clamp((float(count) - 1.0) / 15.0, 0.0, 1.0) * 4.0;
    if (t < 1.0) return vec3(0.0, t, 1.0);
    if (t < 2.0) return vec3(0.0, 1.0, 2.0 - t);
    if (t < 3.0) return vec3(t - 2.0, 1.0, 0.0);
    return vec3(1.0, 4.0 - t, 0.0);
}

// One colour per kind of surface, from the packed vertex data the hit shaders read (util/vertex.glsl). The first
// match wins, in this order; anything else is grey.
vec3 radianteDebugFlagsColor(bool water, bool lava, bool glass, bool pane, bool leaves, bool plant, bool held,
                             bool unlit, bool layer, bool blend) {
    if (water) return vec3(0.1, 0.35, 1.0);
    if (lava) return vec3(1.0, 0.35, 0.0);
    if (glass) return vec3(0.6, 1.0, 1.0);
    if (pane) return vec3(0.3, 0.8, 0.8);
    if (leaves) return vec3(0.1, 0.8, 0.1);
    if (plant) return vec3(0.6, 1.0, 0.2);
    if (held) return vec3(1.0, 0.3, 1.0);
    if (unlit) return vec3(1.0, 1.0, 0.3);
    if (layer) return vec3(0.8, 0.5, 0.2);
    if (blend) return vec3(0.6, 0.4, 1.0);
    return vec3(0.35);
}

#endif
