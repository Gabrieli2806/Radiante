#ifndef VPT_BIOME_FOG_PROFILE_GLSL
#define VPT_BIOME_FOG_PROFILE_GLSL

// The biome fog's height profile and colour, on their own for passes that do not take the whole haze
// (froxel/march.comp). Moved out of biome_haze.glsl as they were. Needs skyUBO.

// How thick the haze is at a world height: full below biomeFogHeights.x, thinning to nothing at .y. Bedrock RTX fogs
// settle in the valleys this way; Radiante's own haze puts both heights far above the world.
float biomeFogHeightProfile(float worldY) {
    vec2 heights = skyUBO.biomeFogHeights.xy;
    return clamp((heights.y - worldY) / max(heights.y - heights.x, 1.0), 0.0, 1.0);
}

// Extinction per channel relative to biomeFog.a: a fog that absorbs more blue than red turns the distance warm.
vec3 biomeFogChroma() {
    return max(skyUBO.biomeFogChroma.rgb, vec3(0.0));
}

#endif
