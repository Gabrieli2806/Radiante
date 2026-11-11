#ifndef UTIL_NUMERIC_GLSL
#define UTIL_NUMERIC_GLSL

// Small numeric helpers every kind of shader can use: no bindings, no stage-specific built-ins. They were written out
// in each file that needed them, and one copy changed without the others is how two shaders come to disagree.

bool finiteFloat(float value) {
    return !isnan(value) && !isinf(value);
}

bool finiteVec3(vec3 value) {
    return !any(isnan(value)) && !any(isinf(value));
}

// Relative luminance of a linear Rec. 709 colour.
float luminance709(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

#endif
