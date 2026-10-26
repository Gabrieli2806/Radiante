#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// Closest hit of entity_solid_z_offset_forward: the same default shading with parallax off, because that group is
// drawn with a forward depth offset and a height-map shift would pull it off its surface.
#define PT_PARALLAX_ALLOWED 0
// The shading fills its hit context from this stage's built-ins (see pt/pt_hit_context.glsl).
#define PT_IN_HIT_STAGE
#include "pt/pt_shade_default.glsl"
