#ifndef RADIANTE_PACK_POST_GLSL
#define RADIANTE_PACK_POST_GLSL

// What a pack's post passes and hooks/post_grade.glsl can read besides the picture: the post stage's uniforms and the
// bloom pyramid. Radiante PT's own post passes do not include it, so they render as before.
//
// Uniforms (set 1, which the post stage always binds): worldUBO holds the pack's live settings, so a "runtime": true
// attribute works in a post pass as in the ray tracing shaders (its define reads worldUBO.packParams); skyUBO holds
// the weather and the biome (common/pack_world.glsl). A pass that declares them itself defines
// RADIANTE_POST_UNIFORMS_DECLARED before including this file.
//
// Bloom (set 3, binding 5): the tone mapping module's bloom pyramid, the exposed HDR picture blurred over five
// levels down to 1/32 and back up to 1/2 of the picture's size. It is drawn only for a pack that sets the
// execution_post global RADIANTE_POST_BLOOM to true (or with Radiante's own Bloom setting on); otherwise the binding
// holds black. Its threshold, spread and level weights are Radiante's bloom settings.

#include "common/shared.hpp"

#ifndef RADIANTE_POST_UNIFORMS_DECLARED
#    define RADIANTE_POST_UNIFORMS_DECLARED
layout(set = 1, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 1, binding = 1) uniform SkyUniform {
    SkyUBO skyUBO;
};
#endif

layout(set = 3, binding = 5) uniform sampler2D radianteBloomImage;

// The bloom at uv: linear light on the scale of the exposed picture before tone mapping (1 is about white), never
// negative. Sample it at offsets for lens effects: a flare mirrored through the centre, ghosts, streaks.
vec3 radianteBloom(vec2 uv) {
    return max(texture(radianteBloomImage, uv).rgb, vec3(0.0));
}

// The bloom as light added to the finished picture, which holds display values: tone mapped with a simple curve so
// the sun does not clip at once, then encoded for the screen. strength 0.05 to 0.3 is a usual glow.
vec3 radianteBloomDisplay(vec2 uv, float strength) {
    vec3 bloom = radianteBloom(uv) * strength;
    return pow(bloom / (1.0 + bloom), vec3(1.0 / 2.2));
}

// Adds the bloom to a display colour as light: decoded to linear, added, encoded again.
vec3 radianteAddBloom(vec3 displayColor, vec2 uv, float strength) {
    vec3 linear = pow(max(displayColor, vec3(0.0)), vec3(2.2));
    vec3 bloom = radianteBloom(uv) * strength;
    vec3 sum = linear + bloom / (1.0 + bloom);
    return pow(sum, vec3(1.0 / 2.2));
}

#endif
