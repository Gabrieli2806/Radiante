#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Closest hit of entity_solid_z_offset_forward: the default surface shading without parallax, because that
// group is drawn with a forward depth offset and a height-map shift would pull it off its surface.
// It only records the hit (record_hit.glsl); the ray generation shader shades it by this kind.
#define PT_HIT_KIND PT_KIND_NO_HEIGHT
#include "record_hit.glsl"
