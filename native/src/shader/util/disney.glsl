// Surface scattering for LabPBR materials: a principled (Burley-style) BSDF with four lobes - diffuse, dielectric
// specular, metal specular, and rough glass (reflection plus transmission) - each a GGX microfacet lobe apart from
// the diffuse one.
//
// Background reading for the models used here:
//   Burley, "Physically Based Shading at Disney" (SIGGRAPH 2012 course notes)
//   Burley, "Extending the Disney BRDF to a BSDF with Integrated Subsurface Scattering" (SIGGRAPH 2015)
//   Walter et al., "Microfacet Models for Refraction through Rough Surfaces" (EGSR 2007)
//   Heitz, "Sampling the GGX Distribution of Visible Normals" (JCGT 2018)
//
// Everything is evaluated in a local frame where the shading normal is +Z. The lobe sampling order and the number
// of random draws per call are part of the renderer's contract (the denoisers and caches see the same sequence
// every frame), so they must stay as they are.

#include "common/shared.hpp"
#include "labpbr.glsl"
#include "random.glsl"

#ifndef DISNEY_GLSL
#    define DISNEY_GLSL

// Local frame (X, Y, Z axes given in world space) -> world.
vec3 ToWorld(vec3 X, vec3 Y, vec3 Z, vec3 V) {
    return V.x * X + V.y * Y + V.z * Z;
}

// World -> local frame.
vec3 ToLocal(vec3 X, vec3 Y, vec3 Z, vec3 V) {
    return vec3(dot(V, X), dot(V, Y), dot(V, Z));
}

// Builds a tangent and bitangent around N. World up is the helper axis unless N is (almost) exactly along it.
void Onb(in vec3 N, inout vec3 T, inout vec3 B) {
    vec3 helper = abs(N.z) < 0.9999999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    T = normalize(cross(helper, N));
    B = cross(N, T);
}

// (1 - u)^5, the Schlick Fresnel falloff.
float SchlickWeight(float u) {
    float oneMinus = clamp(1.0 - u, 0.0, 1.0);
    float sq = oneMinus * oneMinus;
    return sq * sq * oneMinus;
}

// Draws a microfacet normal from the GGX distribution of normals visible from V (local frame, alpha ax / ay).
// Stretch the view to the unit-roughness configuration, pick a point on the projected hemisphere (a disc whose far
// half is squashed by the view's tilt), lift it onto the hemisphere, then unstretch.
vec3 SampleGGXVNDF(vec3 V, float ax, float ay, float r1, float r2) {
    vec3 viewStretched = normalize(vec3(ax * V.x, ay * V.y, V.z));

    float xyLenSq = viewStretched.x * viewStretched.x + viewStretched.y * viewStretched.y;
    vec3 axisA = xyLenSq > 0 ? vec3(-viewStretched.y, viewStretched.x, 0) * inversesqrt(xyLenSq) : vec3(1, 0, 0);
    vec3 axisB = cross(viewStretched, axisA);

    // A uniform point on the unit disc.
    float radius = sqrt(r1);
    float angle = 2.0 * PI * r2;
    float discA = radius * cos(angle);
    float discB = radius * sin(angle);
    // Squash the half of the disc that the tilted view does not see.
    float blend = 0.5 * (1.0 + viewStretched.z);
    discB = (1.0 - blend) * sqrt(1.0 - discA * discA) + blend * discB;

    vec3 normalStretched =
        discA * axisA + discB * axisB + sqrt(max(0.0, 1.0 - discA * discA - discB * discB)) * viewStretched;

    return normalize(vec3(ax * normalStretched.x, ay * normalStretched.y, max(0.0, normalStretched.z)));
}

// The anisotropic GGX (GTR with gamma 2) normal distribution, given the half vector's local components.
float GTR2Aniso(float NDotH, float HDotX, float HDotY, float ax, float ay) {
    float xTerm = HDotX / ax;
    float yTerm = HDotY / ay;
    float sum = xTerm * xTerm + yTerm * yTerm + NDotH * NDotH;
    return 1.0 / (PI * ax * ay * sum * sum);
}

// Smith's masking term for one direction under anisotropic GGX.
float SmithGAniso(float NDotV, float VDotX, float VDotY, float ax, float ay) {
    float xTerm = VDotX * ax;
    float yTerm = VDotY * ay;
    float zTerm = NDotV;
    return (2.0 * NDotV) / (NDotV + sqrt(xTerm * xTerm + yTerm * yTerm + zTerm * zTerm));
}

// A direction from the von Mises-Fisher distribution around mu with concentration kappa (used to jitter towards the
// sun disc). Draws exactly two numbers from seed: first the cosine to mu (inverted CDF), then the angle around it.
vec3 SampleVMF(inout uint seed, vec3 mu, float kappa) {
    kappa = max(kappa, 0.1);

    float xiCos = rand(seed);
    float tail = exp(-2.0 * kappa);
    float cosToMu = 1.0 + (1.0 / kappa) * log(xiCos + (1.0 - xiCos) * tail);

    cosToMu = clamp(cosToMu, -1.0, 1.0);

    float xiAngle = rand(seed);
    float around = 2.0 * PI * xiAngle;
    float alongA = cos(around);
    float alongB = sin(around);

    vec3 helper = abs(mu.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 perpA = normalize(cross(helper, mu));
    vec3 perpB = cross(mu, perpA);

    float sinToMu = sqrt(max(0.0, 1.0 - cosToMu * cosToMu));
    return normalize(cosToMu * mu + sinToMu * (alongA * perpA + alongB * perpB));
}

// Bedrock RTX's specular response, used by the experimental Bedrock profile.
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
#    define VPT_BEDROCK_SPECULAR 1
#else
#    define VPT_BEDROCK_SPECULAR 0
#endif

// How bright a glowing texel of a Bedrock pack appears in that profile, on top of the Emission Brightness setting.
// Bedrock RTX shows it at 5 x (MER green)^2 x its emissive multiplier x the texel's colour, with the multiplier
// read from the running game as 1. The setting defaults to 12, which reaches the shaders as 0.12; 100 / 12 cancels
// that out.
const float VPT_BEDROCK_EMISSIVE_MULTIPLIER = 1.0;
const float VPT_BEDROCK_SEEN_EMISSION = 5.0 * VPT_BEDROCK_EMISSIVE_MULTIPLIER * (100.0 / 12.0);

// GGX alpha for a roughness. Bedrock RTX floors it for any positive roughness (its exact-zero mirror is a separate
// path in that shader). The see-through lobes (glass, translucent blocks) use the same width, since a pack's zero
// roughness otherwise gave them no width at all.
float vptReflectionAlpha(float roughness) {
#if VPT_BEDROCK_SPECULAR
    return max(0.001, roughness * roughness);
#else
    return roughness * roughness;
#endif
}

// Combines the view and light masking terms: a plain product normally, the height-correlated-style combination
// Bedrock RTX was observed to use in its profile.
float vptReflectionMasking(float viewMask, float lightMask) {
#if VPT_BEDROCK_SPECULAR
    return viewMask * lightMask / max(viewMask + lightMask - viewMask * lightMask, 1e-8);
#else
    return viewMask * lightMask;
#endif
}

// Relative luminance with Rec. 709 weights.
float Luminance(vec3 c) {
    return 0.212671 * c.x + 0.715160 * c.y + 0.072169 * c.z;
}

// Exact unpolarised Fresnel reflectance of a dielectric interface. eta is n_incident / n_transmitted.
float DielectricFresnel(float cosThetaI, float eta) {
    float sinTransSq = eta * eta * (1.0f - cosThetaI * cosThetaI);

    // Past the critical angle everything reflects.
    if (sinTransSq > 1.0) return 1.0;

    float cosTrans = sqrt(max(1.0 - sinTransSq, 0.0));

    float perpendicular = (eta * cosTrans - cosThetaI) / (eta * cosTrans + cosThetaI);
    float parallel = (eta * cosThetaI - cosTrans) / (eta * cosThetaI + cosTrans);

    return 0.5f * (perpendicular * perpendicular + parallel * parallel);
}

// Reflectance of a glass or water interface, for the camera-visible glass/water transport (not the microfacet
// lobes).
//
// Bedrock compatibility: approximates Bedrock RTX's observed behaviour for the shape; the critical-angle cosine
// derived from the two indices is INFERRED (Bedrock reads it from a table whose values are not known). It is
// Schlick's approximation with the incidence cosine remapped to reach 1 exactly at the critical angle, which keeps
// total internal reflection continuous. With eta = n_incident / n_transmitted:
//   F0 = ((1 - eta) / (1 + eta))^2,   cc = sqrt(1 - 1/eta^2) when eta > 1 (else 0)
//   t  = clamp(1 + (cos - 1) / (1 - cc), 0, 1),   F = F0 + (1 - F0) * (1 - t)^5
// Model 0 is the exact dielectric Fresnel Radiante has always used.
#ifndef VPT_FRESNEL_MODEL
#    define VPT_FRESNEL_MODEL 0
#endif
#include "util/bedrock_transport.glsl"

float vptInterfaceFresnel(float cosThetaI, float eta) {
#if defined(VPT_BEDROCK_ORIGINAL) && VPT_BEDROCK_ORIGINAL != 0
    return bedrockInterfaceFresnel(cosThetaI, eta);
#elif VPT_FRESNEL_MODEL != 0
    float f0 = (1.0 - eta) / (1.0 + eta);
    f0 *= f0;
    float cosCritical = eta > 1.0 ? sqrt(max(1.0 - 1.0 / (eta * eta), 0.0)) : 0.0;
    float t = clamp(1.0 + (cosThetaI - 1.0) / max(1.0 - cosCritical, 1e-4), 0.0, 1.0);
    return clamp(f0 + (1.0 - f0) * pow(1.0 - t, 5.0), 0.0, 1.0);
#else
    return DielectricFresnel(cosThetaI, eta);
#endif
}

// Cosine-weighted direction on the +Z hemisphere: a uniform disc point lifted straight up.
vec3 CosineSampleHemisphere(float r1, float r2) {
    vec3 dir;
    float radius = sqrt(r1);
    float angle = TWO_PI * r2;
    dir.x = radius * cos(angle);
    dir.y = radius * sin(angle);
    dir.z = sqrt(max(0.0, 1.0 - dir.x * dir.x - dir.y * dir.y));
    return dir;
}

// Value (times |cos| of L) and sampling pdf of the whole BSDF for view V and light L around shading normal N. The pdf
// matches DisneySample's lobe selection, so the two can be combined with MIS.
vec3 DisneyEval(LabPBRMat mat, vec3 V, vec3 N, vec3 L, out float pdf) {
    pdf = 0.0;
    vec3 result = vec3(0.0);

    vec3 tangent, bitangent;
    Onb(N, tangent, bitangent);

    vec3 wo = ToLocal(tangent, bitangent, N, V);
    vec3 wi = ToLocal(tangent, bitangent, N, L);

    // Relative index across the surface, depending on which side the view is on.
    float eta = (dot(V, N) > 0.0) ? (1.0 / mat.ior) : mat.ior;

    // Reflection uses the plain half vector; transmission the generalised one of Walter et al.
    vec3 wh;
    if (wi.z > 0.0)
        wh = normalize(wi + wo);
    else
        wh = normalize(wi + wo * eta);

    if (wh.z < 0.0) wh = -wh;

    // How much of the surface each kind of material is.
    float dielectricShare = (1.0 - mat.metallic) * (1.0 - mat.transmission);
    float metalShare = mat.metallic;
    float glassShare = (1.0 - mat.metallic) * mat.transmission;

    // Lobe selection probabilities, by each lobe's expected contribution.
    float viewFresnel = SchlickWeight(abs(wo.z));

    float diffuseShare = dielectricShare;
#if VPT_BEDROCK_SPECULAR
    diffuseShare *= max(1.0 - mat.emission, 0.0);
#endif
    float pDiffuse = diffuseShare * Luminance(mat.albedo);
    float pDielectric = dielectricShare * Luminance(mix(mat.f0, vec3(1.0), viewFresnel));
    float pMetal = metalShare * Luminance(mix(mat.albedo, vec3(1.0), viewFresnel));
    float pGlass = glassShare;

    float normaliser = 1.0 / (pDiffuse + pDielectric + pMetal + pGlass + 1e-5);
    pDiffuse *= normaliser;
    pDielectric *= normaliser;
    pMetal *= normaliser;
    pGlass *= normaliser;

    bool sameSide = wi.z * wo.z > 0.0;
    float lobePdf = 0.0;
    float cosViewHalf = abs(dot(wo, wh));

    // Burley diffuse with retro-reflection, blended towards the fake subsurface term by mat.subSurface.
    if (pDiffuse > 0.0 && sameSide) {
        float cosLightHalf = dot(wi, wh);
        float retroRough = 2.0 * mat.roughness * cosLightHalf * cosLightHalf;
        float fresnelL = SchlickWeight(wi.z);
        float fresnelV = SchlickWeight(wo.z);
        float retro = retroRough * (fresnelL + fresnelV + fresnelL * fresnelV * (retroRough - 1.0));
        float lambertFalloff = (1.0 - 0.5 * fresnelL) * (1.0 - 0.5 * fresnelV);

        // Hanrahan-Krueger-like flattening for thin subsurface.
        float subsurface90 = 0.5 * retroRough;
        float subsurfaceFresnel = mix(1.0, subsurface90, fresnelL) * mix(1.0, subsurface90, fresnelV);
        float subsurface = 1.25 * (subsurfaceFresnel * (1.0 / (wi.z + wo.z) - 0.5) + 0.5);

        vec3 diffuse = INV_PI * mat.albedo * mix(lambertFalloff + retro, subsurface, mat.subSurface);

        result += diffuse * diffuseShare;
        pdf += (wi.z * INV_PI) * pDiffuse; // cosine-weighted hemisphere
    }

    // Specular reflection off a dielectric coat.
    if (pDielectric > 0.0 && sameSide) {
        float fresnel = DielectricFresnel(cosViewHalf, 1.0 / mat.ior);

        float alpha = vptReflectionAlpha(mat.roughness); // isotropic
        float distribution = GTR2Aniso(wh.z, wh.x, wh.y, alpha, alpha);
        float maskView = SmithGAniso(abs(wo.z), wo.x, wo.y, alpha, alpha);
        float maskBoth = vptReflectionMasking(maskView, SmithGAniso(abs(wi.z), wi.x, wi.y, alpha, alpha));

        lobePdf = maskView * distribution / (4.0 * wo.z);
        vec3 specular = vec3(fresnel) * distribution * maskBoth / (4.0 * wi.z * wo.z);

        result += specular * dielectricShare;
        pdf += lobePdf * pDielectric;
    }

    // Specular reflection off metal, tinted by the albedo with a Schlick edge.
    if (pMetal > 0.0 && sameSide) {
        vec3 metalFresnel = mix(mat.albedo, vec3(1.0), SchlickWeight(cosViewHalf));

        float alpha = vptReflectionAlpha(mat.roughness);
        float distribution = GTR2Aniso(wh.z, wh.x, wh.y, alpha, alpha);
        float maskView = SmithGAniso(abs(wo.z), wo.x, wo.y, alpha, alpha);
        float maskBoth = vptReflectionMasking(maskView, SmithGAniso(abs(wi.z), wi.x, wi.y, alpha, alpha));

        lobePdf = maskView * distribution / (4.0 * wo.z);
        vec3 specular = metalFresnel * distribution * maskBoth / (4.0 * wi.z * wo.z);

        result += specular * metalShare;
        pdf += lobePdf * pMetal;
    }

    // Rough glass: reflects with probability F, transmits otherwise.
    if (pGlass > 0.0) {
        float fresnel = DielectricFresnel(cosViewHalf, eta);
        float alpha = vptReflectionAlpha(mat.roughness);
        float distribution = GTR2Aniso(wh.z, wh.x, wh.y, alpha, alpha);
        float maskView = SmithGAniso(abs(wo.z), wo.x, wo.y, alpha, alpha);
        float maskBoth = maskView * SmithGAniso(abs(wi.z), wi.x, wi.y, alpha, alpha);

        if (sameSide) {
            lobePdf = maskView * distribution / (4.0 * wo.z);
            result += vec3(fresnel) * distribution * maskBoth / (4.0 * wi.z * wo.z) * glassShare;
            pdf += lobePdf * pGlass * fresnel;
        } else {
            // Change of variables from half vector to refracted direction.
            float halfDenom = dot(wi, wh) + dot(wo, wh) * eta;
            halfDenom *= halfDenom;
            float dwhDwi = abs(dot(wi, wh)) / halfDenom;

            lobePdf = maskView * max(0.0, cosViewHalf) * distribution * dwhDwi / wo.z;

            // The square root of the albedo, since light usually crosses two faces of a see-through block.
            vec3 transmitted = pow(mat.albedo, vec3(0.5)) * (1.0 - fresnel) * distribution * maskBoth *
                               abs(dot(wo, wh)) * dwhDwi * (eta * eta) / abs(wi.z * wo.z);

            result += transmitted * glassShare;
            pdf += lobePdf * pGlass * (1.0 - fresnel);
        }
    }

    return result * abs(wi.z); // include the cosine term
}

// Picks a lobe and a direction L for view V, then evaluates the full BSDF there (value and pdf from DisneyEval).
// Always draws three numbers from seed, in this order: two for the direction, one for the lobe. lobeType reports
// 0 diffuse, 1 reflection, 2 transmission.
vec3 DisneySample(LabPBRMat mat, vec3 V, vec3 N, out vec3 L, out float pdf, inout uint seed, out uint lobeType) {
    pdf = 0.0;
    vec3 tangent, bitangent;
    Onb(N, tangent, bitangent);

    vec3 wo = ToLocal(tangent, bitangent, N, V);
    float xiA = rand(seed);
    float xiB = rand(seed);
    float xiLobe = rand(seed);

    // The same lobe probabilities as DisneyEval.
    float dielectricShare = (1.0 - mat.metallic) * (1.0 - mat.transmission);
    float metalShare = mat.metallic;
    float glassShare = (1.0 - mat.metallic) * mat.transmission;
    float viewFresnel = SchlickWeight(abs(wo.z));

    float diffuseShare = dielectricShare;
#if VPT_BEDROCK_SPECULAR
    diffuseShare *= max(1.0 - mat.emission, 0.0);
#endif
    float pDiffuse = diffuseShare * Luminance(mat.albedo);
    float pDielectric = dielectricShare * Luminance(mix(mat.f0, vec3(1.0), viewFresnel));
    float pMetal = metalShare * Luminance(mix(mat.albedo, vec3(1.0), viewFresnel));
    float pGlass = glassShare;

    float normaliser = 1.0 / (pDiffuse + pDielectric + pMetal + pGlass + 1e-5);
    pDiffuse *= normaliser;
    pDielectric *= normaliser;
    pMetal *= normaliser;
    pGlass *= normaliser;

    float upToDiffuse = pDiffuse;
    float upToDielectric = upToDiffuse + pDielectric;
    float upToMetal = upToDielectric + pMetal;

    vec3 wi;

    if (xiLobe < upToDiffuse) {
        lobeType = 0;
        wi = CosineSampleHemisphere(xiA, xiB);
    } else if (xiLobe < upToMetal) {
        // Dielectric and metal reflection share the same GGX sampling.
        lobeType = 1;
        float alpha = vptReflectionAlpha(mat.roughness); // isotropic
        vec3 wh = SampleGGXVNDF(wo, alpha, alpha, xiA, xiB);
        if (wh.z < 0.0) wh = -wh;
        wi = normalize(reflect(-wo, wh));
    } else {
        float alpha = vptReflectionAlpha(mat.roughness); // isotropic
        vec3 wh = SampleGGXVNDF(wo, alpha, alpha, xiA, xiB);
        if (wh.z < 0.0) wh = -wh;

        float eta = (wo.z > 0.0) ? (1.0 / mat.ior) : mat.ior;
        float fresnel = DielectricFresnel(abs(dot(wo, wh)), eta);

        // Reuse the lobe number, stretched over the glass interval, to choose reflection or refraction.
        float xiGlass = (xiLobe - upToMetal) / (1.0 - upToMetal + 1e-5);

        if (xiGlass < fresnel) {
            lobeType = 1;
            wi = normalize(reflect(-wo, wh));
        } else {
            lobeType = 2;
            wi = normalize(refract(-wo, wh, eta));
        }
    }

    L = ToWorld(tangent, bitangent, N, wi);
    V = ToWorld(tangent, bitangent, N, wo);

    return DisneyEval(mat, V, N, L, pdf);
}

// Samples only the rough glass transmission lobe (for the camera seeing through glass). Retries up to four
// microfacet normals, two random numbers each, until one refracts instead of reflecting totally. Returns zero when
// none does or the material does not transmit.
vec3 DisneySampleSpecularRefraction(LabPBRMat mat, vec3 V, vec3 N, out vec3 L, out float pdf, inout uint seed) {
    pdf = 0.0;
    L = vec3(0.0);

    vec3 tangent, bitangent;
    Onb(N, tangent, bitangent);

    vec3 wo = ToLocal(tangent, bitangent, N, V);
    float cosView = abs(wo.z);
    if (cosView <= 1e-6 || mat.transmission <= 0.0 || mat.metallic >= 1.0) { return vec3(0.0); }

    float alpha = vptReflectionAlpha(mat.roughness);
    float eta = (wo.z > 0.0) ? (1.0 / mat.ior) : mat.ior;
    vec3 wh = vec3(0.0);
    vec3 wi = vec3(0.0);

    for (int tries = 0; tries < 4; ++tries) {
        float xiA = rand(seed);
        float xiB = rand(seed);
        wh = SampleGGXVNDF(wo, alpha, alpha, xiA, xiB);
        if (wh.z < 0.0) wh = -wh;

        wi = refract(-wo, wh, eta);
        if (dot(wi, wi) > 1e-6) {
            wi = normalize(wi);
            break;
        }
    }

    if (dot(wi, wi) <= 1e-6) { return vec3(0.0); }

    float cosViewHalf = abs(dot(wo, wh));
    float fresnel = DielectricFresnel(cosViewHalf, eta);
    float distribution = GTR2Aniso(wh.z, wh.x, wh.y, alpha, alpha);
    float maskView = SmithGAniso(cosView, wo.x, wo.y, alpha, alpha);
    float maskBoth = maskView * SmithGAniso(abs(wi.z), wi.x, wi.y, alpha, alpha);

    float halfDenom = dot(wi, wh) + dot(wo, wh) * eta;
    halfDenom *= halfDenom;
    if (halfDenom <= 1e-10) { return vec3(0.0); }

    float dwhDwi = abs(dot(wi, wh)) / halfDenom;
    pdf = maskView * max(0.0, cosViewHalf) * distribution * dwhDwi / cosView;
    if (pdf <= 1e-6) { return vec3(0.0); }

    vec3 transmitted = pow(max(mat.albedo, vec3(0.0)), vec3(0.5)) * (1.0 - fresnel) * distribution * maskBoth *
                       abs(dot(wo, wh)) * dwhDwi * (eta * eta) / max(abs(wi.z * wo.z), 1e-6);
    vec3 value = transmitted * ((1.0 - mat.metallic) * mat.transmission);

    L = ToWorld(tangent, bitangent, N, wi);
    return value * abs(wi.z);
}

#endif
