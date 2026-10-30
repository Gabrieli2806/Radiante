#ifndef RADIANTE_HOOK_POST_GRADE_GLSL
#define RADIANTE_HOOK_POST_GRADE_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// Called per pixel on the finished picture, after tone mapping, motion blur and depth of field, before the HUD
// (post_render/post_hook_grade.frag). Only runs when the pack turns it on: set the execution_post global
// RADIANTE_HOOK_POST_GRADE to true in the pack's configs.json; off, radiante-pt runs no extra pass.
//   color display colour, 0 to 1
//   uv    screen position, 0 to 1 (0,0 at the top left)
// The picture itself is the sampler hookPostGradeInput, declared before this file, for effects that read
// neighbouring pixels (textureSize(hookPostGradeInput, 0) gives its size).
void hookPostGrade(inout vec3 color, vec2 uv) {}

#endif
