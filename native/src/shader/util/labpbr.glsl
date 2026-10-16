#ifndef LABPBR_GLSL
#define LABPBR_GLSL

// Decoding of LabPBR 1.3 resource-pack textures (colour, specular "_s" and normal "_n" maps) into the material the
// BSDF in disney.glsl works with, plus the colour rules for see-through blocks.

#define EPS 1e-6

struct LabPBRMat {
    vec3 albedo;
    vec3 f0;
    float roughness;
    float metallic;
    float subSurface;
    float transmission;
    float ior;
    float emission;
    vec3 normal;
    float ao;
    float height;
};

// Normal-incidence reflectance of a conductor from its complex index n + ik, per colour channel.
vec3 CalculateF0(vec3 n, vec3 k) {
    vec3 kSq = max(k * k, vec3(1e-6));
    vec3 f0 = ((n - 1.0) * (n - 1.0) + kSq) / ((n + 1.0) * (n + 1.0) + kSq);
    return f0;
}

// How much a glass colour reads as dull, dark glass (tinted glass) rather than coloured glass, from 0 to 1: low
// saturation and low brightness both count.
float glassDarkness(vec3 colour) {
    float brightest = max(colour.r, max(colour.g, colour.b));
    float darkest = min(colour.r, min(colour.g, colour.b));
    float saturation = brightest > 1e-3 ? (brightest - darkest) / brightest : 0.0;
    return (1.0 - smoothstep(0.4, 0.65, saturation)) * (1.0 - smoothstep(0.35, 0.6, brightest));
}

// The colour light picks up crossing a see-through block, after Bedrock RTX: the texture's hue at full brightness,
// applied as strongly as its alpha says the block is coloured. A stained pane's texture is a dark colour, and
// filtering by it as stored let hardly any light through, where Bedrock's stained glass throws bright clear colour.
vec3 glassTint(vec3 colour, float alpha) {
    float brightest = max(colour.r, max(colour.g, colour.b));
    vec3 hue = brightest > 1e-3 ? colour / brightest : vec3(1.0);
    // Tinted glass is meant to be dark, so it keeps part of its darkness and shows the world dimly instead of as
    // clearly as stained glass; coloured glass filters by its hue alone. Light crosses two faces of a block and each
    // applies this, so only a few percent make it through tinted glass.
    hue *= mix(1.0, pow(max(brightest, 0.0), 0.45), glassDarkness(colour));
    return mix(vec3(1.0), hue, smoothstep(0.0, 0.3, alpha));
}

#ifndef VPT_GLASS_TINT_MODEL
#    define VPT_GLASS_TINT_MODEL 0
#endif

// The colour a view ray picks up through one see-through surface, Bedrock style (VPT_GLASS_TINT_MODEL). This is the
// rule Bedrock's sun shadow applies to the same surfaces (observed; that camera rays get the same filter is
// INFERRED): the texture colour as stored, scaled by how clear the surface is, where the lower half of the alpha
// range counts as fully clear: colour * (1 - saturate(2 alpha - 1)).
vec3 viewGlassTintBedrock(vec3 colour, float alpha) {
    // A texel with no coverage has no colour of its own (Java textures leave such texels black or arbitrary, unlike
    // Bedrock's authored ones), so it is clear.
    if (alpha <= 1.0 / 255.0) { return vec3(1.0); }
    return colour * (1.0 - clamp(alpha * 2.0 - 1.0, 0.0, 1.0));
}

vec3 viewGlassTint(vec3 colour, float alpha) {
#if VPT_GLASS_TINT_MODEL != 0
    return viewGlassTintBedrock(colour, alpha);
#else
    return glassTint(colour, alpha);
#endif
}

// The longest (squared) a normal map's RG pair may be and still count as a normal: slightly over one, to allow for
// maps authored or quantised a little long.
const float NORMAL_MAP_MAX_LENGTH2 = 1.21;

// Complex refractive indices (n, k at roughly R, G, B) of the eight metals LabPBR reserves specular-green values
// 230-237 for. Any other index leaves n = 1, k = 0.
void labpbrPredefinedMetal(int index, inout vec3 n, inout vec3 k) {
    if (index == 230) { // iron
        n = vec3(2.9114, 2.9497, 2.5845);
        k = vec3(3.0893, 2.9318, 2.7670);
    } else if (index == 231) { // gold
        n = vec3(0.18299, 0.42108, 1.3734);
        k = vec3(3.4242, 2.3459, 1.7704);
    } else if (index == 232) { // aluminium
        n = vec3(1.3456, 0.96521, 0.61722);
        k = vec3(7.4746, 6.3995, 5.3031);
    } else if (index == 233) { // chrome
        n = vec3(3.1071, 3.1812, 2.3230);
        k = vec3(3.3314, 3.3291, 3.1350);
    } else if (index == 234) { // copper
        n = vec3(0.27105, 0.67693, 1.3164);
        k = vec3(3.6092, 2.6248, 2.2921);
    } else if (index == 235) { // lead
        n = vec3(1.9100, 1.8300, 1.4400);
        k = vec3(3.5100, 3.4000, 3.1800);
    } else if (index == 236) { // platinum
        n = vec3(2.3757, 2.0847, 1.8453);
        k = vec3(4.2655, 3.7153, 3.1365);
    } else if (index == 237) { // silver
        n = vec3(0.15943, 0.14512, 0.13547);
        k = vec3(3.9291, 3.1900, 2.3808);
    }
}

// Builds the material from the three texels. LabPBR channels:
//   specular R  perceptual smoothness        specular G  F0 (below 230) or a metal id (230 and up)
//   specular B  porosity / subsurface (65+)  specular A  emission (255 means none)
//   normal RG   tangent-space normal XY      normal B    ambient occlusion slot (read from R here)  normal A  height
LabPBRMat convertLabPBRMaterial(vec4 texAlbedo, vec4 texSpecular, vec4 texNormal) {
    LabPBRMat mat;

    // Smoothness to roughness, then kept above a small floor (255 is meant to be very smooth, e.g. polished
    // granite, not a perfect mirror).
    mat.roughness = pow(1.0 - texSpecular.r, 2.0);
    mat.roughness = mix(0.01, 1.0, mat.roughness);

    // Blue values below 65 are porosity, which is not used; 65-255 map to subsurface 0-1.
    float subsurfaceStart = 65.0 / 255.0;
    mat.subSurface = texSpecular.b < subsurfaceStart ? 0.0 : (texSpecular.b - subsurfaceStart) / (1.0 - subsurfaceStart);

    int greenByte = int(round(texSpecular.g * 255.0));

    mat.metallic = 0.0;
    mat.transmission = 0.0;
    mat.ior = 1.5;
    mat.f0 = vec3(0.04);

    int emissionByte = int(round(texSpecular.a * 255.0));
    if (emissionByte == 255) {
        mat.emission = 0;
    } else {
        mat.emission = emissionByte / 254.0;
#if defined(VPT_EMISSION_MODEL) && VPT_EMISSION_MODEL != 0
        // Bedrock compatibility (observed: emission = MER green squared). Converted Bedrock packs store 4 x green,
        // capped at the top of LabPBR's range, so squaring the stored value brings back Bedrock's square law exactly
        // up to green 0.25 and keeps brighter sources at full strength (the multiplier above that is a run-time
        // value that is not known).
        mat.emission *= mat.emission;
#endif
        // Lamps, torches and glowstone slightly softer than the texture says: they lit rooms too strongly at night.
        mat.emission *= 0.55;
    }

    if (greenByte < 230) {
        // A dielectric whose F0 is stored directly.
        mat.metallic = 0.0;
        mat.albedo = texAlbedo.rgb;

        float specularValue = texSpecular.g;
        float F0 = max(specularValue, 0.02); // LabPBR's lower bound
        mat.f0 = vec3(F0);

        // The index of refraction that gives this F0 at normal incidence.
        float rootF0 = sqrt(F0);
        mat.ior = (1.0 + rootF0) / max(1.0 - rootF0, EPS);

        if (texAlbedo.a < 1.0 - EPS) {
            mat.transmission = 1.0;
            // Vanilla blends see-through blocks by alpha, so a half-opaque red pane still lets half of the scene
            // through untinted. Filtering by the full saturated colour made stained and tinted glass look almost
            // opaque; the alpha decides how strongly the colour filters instead.
            mat.albedo = glassTint(texAlbedo.rgb, texAlbedo.a);
            // A see-through block with subsurface authored (Radiante's own ice maps) is a translucent solid rather
            // than glass, and its alpha says how much of it is solid. The surface is always hit and shaded and only
            // the remainder of the light passes through - vanilla's blend without per-pixel hit-or-miss noise.
            if (mat.subSurface > 0.0) {
                mat.transmission = 1.0 - texAlbedo.a;
                mat.albedo = texAlbedo.rgb;
            }
            // Vanilla glass, stained glass and panes come without a specular map, and a missing map decodes as fully
            // rough, which scatters the light going through them so they look frosted. With nothing authored, a
            // see-through block is taken to be smooth glass.
            if (texSpecular.r <= 0.0 && texSpecular.g <= 0.0 && texSpecular.b <= 0.0 && texSpecular.a <= 0.0) {
                mat.roughness = 0.02;
                mat.f0 = vec3(0.04);
                mat.ior = 1.5;
            }
        }
    } else if (greenByte <= 237) {
        // One of LabPBR's predefined metals: reflectance from measured optical constants, no diffuse.
        vec3 n = vec3(1.0);
        vec3 k = vec3(0.0);
        labpbrPredefinedMetal(greenByte, n, k);

        mat.metallic = 1.0;
        mat.f0 = CalculateF0(n, k);
        mat.albedo = mat.f0;
    } else if (greenByte < 255) {
        // 238-254, left unassigned by LabPBR: partly metallic, (value - 237) / 18. Bedrock RTX packs blend
        // metalness smoothly (gems sit near one half), and converted as strictly metal or not their surfaces broke
        // up into mirror and plain texels. Only Radiante's Bedrock conversion writes these values.
        mat.metallic = float(greenByte - 237) / 18.0;
        mat.albedo = texAlbedo.rgb;
        mat.f0 = mix(vec3(0.04), texAlbedo.rgb, mat.metallic);
    } else {
        // 255: a metal coloured by its albedo texture.
        mat.metallic = 1.0;
        mat.albedo = texAlbedo.rgb;
        mat.f0 = texAlbedo.rgb;
    }

    // The normal map stores XY in 0..1; Z is rebuilt so the vector has unit length.
    mat.normal.xy = texNormal.xy * 2.0 - 1.0;
    float xyLengthSq = dot(mat.normal.xy, mat.normal.xy);
    // An RG pair longer than one (possible in a Bedrock normal map read as authored) would take the square root of a
    // negative number and give a NaN normal, hence the guards.
    mat.normal.z = max(sqrt(max(1.0 - xyLengthSq, 0.0)), 0.01);
    // Much longer than one is not a normal at all: a texture with no normal map, or an empty texel of one, reads
    // (0, 0) and decodes to (-1, -1). Used as a normal it lay flat along the texture's diagonal and faced away from
    // any view down that diagonal, so smooth floors went dark in rings, in a disc ahead of the camera looking that
    // way. Use the face's own normal instead.
    if (xyLengthSq > NORMAL_MAP_MAX_LENGTH2) { mat.normal = vec3(0.0, 0.0, 1.0); }

    mat.ao = texNormal.x;
    mat.height = texNormal.w;

    return mat;
}

#endif
