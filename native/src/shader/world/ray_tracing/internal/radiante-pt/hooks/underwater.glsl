#ifndef RADIANTE_HOOK_UNDERWATER_GLSL
#define RADIANTE_HOOK_UNDERWATER_GLSL

// Hook point (shader packs that extend radiante-pt replace this file). Part of the pack interface: the signature
// stays as it is within radiante-pt@1.
//
// The finished picture while the camera is under water (post_render/post_hook_grade.frag), per pixel, before
// hookPostGrade: for a wavy distortion, a tint or a vignette under water. Runs in the same pass as hookPostGrade, so
// only when the pack turns that pass on (the execution_post global RADIANTE_HOOK_POST_GRADE set to true); a pack
// that only wants this hook keeps hooks/post_grade.glsl as it is.
//   color display colour, 0 to 1, after tone mapping
//   uv    screen position, 0 to 1 (0,0 at the top left)
// The picture is the sampler hookPostGradeInput: texture(hookPostGradeInput, uv + offset).rgb reads a neighbouring
// pixel, which is how a distortion moves the picture. worldUBO.gameTime * 1200.0 is the world's clock in seconds.
void hookUnderwater(inout vec3 color, vec2 uv) {}

// Only in this default file: radiante-pt then skips the call, so its shaders are the same as before the hook
// existed. A pack's file must not define it.
#define RADIANTE_HOOK_UNDERWATER_DEFAULT 1

#endif
