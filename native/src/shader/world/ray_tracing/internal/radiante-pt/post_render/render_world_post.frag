#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

// Fragment stage for rasterised world geometry. Untextured fragments are dropped (alpha stays 0). Emissive texels
// (LabPBR emission in the specular map's alpha: 0..254 scaled to 0..1, 255 meaning "not emissive") are drawn at
// their emission strength and ignore the light map; everything else is lit by the vanilla light map when asked.

#include "common/shared.hpp"

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(set = 1, binding = 0) uniform WorldUniform {
    WorldUBO worldUBO;
};

layout(set = 1, binding = 1) uniform SkyUniform {
    SkyUBO skyUBO;
};

layout(set = 2, binding = 0) readonly buffer TextureMappingBuffer {
    TextureMapping mapping;
};

layout(location = 0) in vec3 pos;
layout(location = 1) flat in uint useNorm;
layout(location = 2) in vec3 norm;
layout(location = 3) flat in uint useColorLayer;
layout(location = 4) in vec4 colorLayer;
layout(location = 5) flat in uint useTexture;
layout(location = 6) flat in uint useOverlay;
layout(location = 7) in vec2 textureUV;
layout(location = 8) flat in ivec2 overlayUV;
layout(location = 9) flat in uint useGlint;
layout(location = 10) flat in uint textureID;
layout(location = 11) in vec2 glintUV;
layout(location = 12) flat in uint glintTexture;
layout(location = 13) flat in uint useLight;
layout(location = 14) flat in ivec2 lightUV;
layout(location = 15) in vec4 lightMapColor;
layout(location = 16) in vec4 overlayColor;

layout(location = 0) out vec4 fragColor;

// LabPBR emission from the specular texture's alpha; 0 when there is no specular texture or the texel says 255.
float labPbrEmission(int specularID) {
    if (specularID < 0) { return 0.0; }
    int encoded = int(round(texture(textures[nonuniformEXT(specularID)], textureUV).a * 255.0));
    return encoded == 255 ? 0.0 : encoded / 254.0;
}

void main() {
    int specularID = mapping.entries[textureID].specular;
    vec4 albedo = vec4(0.0);
    float emission = 0.0;
    if (useTexture > 0) {
        albedo = texture(textures[nonuniformEXT(textureID)], textureUV);
        emission = labPbrEmission(specularID);
    }
    if (albedo.a < 0.1) { discard; }
    if (useColorLayer > 0) { albedo *= colorLayer; }
    if (useOverlay > 0) { albedo.rgb = mix(overlayColor.rgb, albedo.rgb, overlayColor.a); }

    if (emission != 0.0) {
        fragColor = albedo * emission;
    } else {
        fragColor = useLight == 0 ? albedo : albedo * lightMapColor;
    }

    // Rotation-only view transform (the position is camera-relative); depth spread linearly over 1000 m.
    float viewDepth = -(mat4(mat3(worldUBO.cameraEffectedViewMat)) * vec4(pos, 1.0)).z;
    gl_FragDepth = clamp(viewDepth / 1000.0, 0.0, 1.0);
}
