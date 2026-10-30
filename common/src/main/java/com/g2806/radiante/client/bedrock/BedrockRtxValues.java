package com.g2806.radiante.client.bedrock;

import com.g2806.radiante.client.pipeline.Pipeline;

import java.util.List;

/**
 * The settings that make radiante-pt look like Bedrock RTX, in one place, with how well each value is known. They are
 * the plain defaults of the shader pack (radiante-pt's configs.json) and of the tone mapping module
 * (modules/tone_mapping.yaml); this table repeats them so that the settings of an older version can be put back to
 * them (OptionsMigration) and so that a test can check that the defaults still say the same.
 *
 * <p>Each entry says how well its value is known ({@link ParameterStatus}). Structure known to be Bedrock RTX's is
 * implemented; behaviour inferred from it is implemented and said to be inferred; numbers nobody has measured are
 * exposed, kept conservative, and marked {@link ParameterStatus#APPROXIMATE} or {@link ParameterStatus#UNKNOWN}.
 */
public final class BedrockRtxValues {

    private static final String TONE = Pipeline.TONE_MAPPING_MODULE_NAME;
    private static final String A = "render_pipeline.module.tone_mapping.attribute.";
    private static final String TRUE = "render_pipeline.true";
    private static final String FALSE = "render_pipeline.false";

    private static final String RT = "render_pipeline.module.ray_tracing.attribute.";

    private static final String NOT_MEASURED = "no reference capture yet";

    /** The Bedrock RTX values, grouped by the part of the renderer they belong to. */
    public static final List<CompatParameter> PARAMETERS = List.of(
        new CompatParameter("hands", null, RT + "hand_samples", "3", ParameterStatus.APPROXIMATE,
            "Radiante's existing hand-lighting sample count; not a measured Bedrock RTX value"),
        // ---- mode
        new CompatParameter("mode", null, Pipeline.BEDROCK_ORIGINAL_ATTRIBUTE, TRUE, ParameterStatus.MATCHES_BEDROCK,
            "Bedrock RTX's own sky, sunlight, exposure and materials"),
        new CompatParameter("mode", null, Pipeline.BEDROCK_ENHANCED_ATTRIBUTE, TRUE, ParameterStatus.INFERRED,
            "the few things that go beyond Bedrock on purpose: light blocks lit by their own light, the end portal's "
                + "glow"),
        new CompatParameter("mode", null, Pipeline.BEDROCK_MATERIALS_ATTRIBUTE, TRUE, ParameterStatus.MATCHES_BEDROCK,
            "the pack's own metalness and roughness, which its specular response is made for"),
        new CompatParameter("mode", null, Pipeline.BEDROCK_ATMOSPHERE_ATTRIBUTE, TRUE, ParameterStatus.MATCHES_BEDROCK,
            "Bedrock's sky and sunlight"),
        new CompatParameter("mode", null, Pipeline.TRANSPARENT_CHECKERBOARD_ATTRIBUTE, FALSE, ParameterStatus.INFERRED,
            "both branches of water and glass traced: full-resolution DLSS guides do not describe alternating "
                + "missing lobes"),
        new CompatParameter("mode", null, Pipeline.SEAMLESS_GLASS_ATTRIBUTE, FALSE, ParameterStatus.MATCHES_BEDROCK,
            "glass as the pack draws it, each block in its frame"),
        new CompatParameter("mode", null, Pipeline.FACE_SHADING_ATTRIBUTE, FALSE, ParameterStatus.MATCHES_BEDROCK,
            "Bedrock RTX has no fixed face shading"),

        // ---- exposure
        new CompatParameter("exposure", TONE, A + "enable_auto_exposure", TRUE, ParameterStatus.VERIFIED,
            "Bedrock builds its exposure from a luminance histogram"),
        new CompatParameter("exposure", TONE, A + "exposure_adaptation", "1.0", ParameterStatus.INFERRED,
            "the eye adapts fully: a room lit through a window is brought up to a readable level, as Bedrock does"),
        new CompatParameter("exposure", TONE, A + "max_exposure", "32.0", ParameterStatus.INFERRED,
            "how far the eye may open in the dark: a room lit through a window comes up about as bright as in "
                + "Bedrock (compared side by side); the real limit is a run-time value"),
        new CompatParameter("exposure", TONE, A + "exposure_metering_mode", A + "exposure_metering_mode.bedrock_original",
            ParameterStatus.BEDROCK_VALUE,
            "Bedrock's light meter; the exposure range and the speed it adapts at are the metering mode's own "
                + "(tone_mapping/exposure.comp)"),
        new CompatParameter("exposure", TONE, A + "exposure_bias", "0.0", ParameterStatus.BEDROCK_VALUE,
            "Radiante's brightness control on top of the curve; Bedrock has none, so it is neutral"),

        // ---- tone mapping: Bedrock RTX's own values (its frame constants and its tone mapping material's uniforms,
        // the same at every time of day)
        new CompatParameter("toneMapping", TONE, A + "method", A + "method.bedrock_provisional",
            ParameterStatus.MATCHES_BEDROCK,
            "the curve is built from a luminance histogram as Bedrock's is, and applied with the tone mapping "
                + "material's grade"),
        new CompatParameter("toneMapping", TONE, A + "curve_dynamic_range", "15.0", ParameterStatus.BEDROCK_VALUE,
            "toneMappingDynamicRange: stops from white to black of the curve"),
        new CompatParameter("toneMapping", TONE, A + "curve_shift", "0.05", ParameterStatus.BEDROCK_VALUE,
            "toneMappingCurveShift: added to the curve, in stops"),
        new CompatParameter("toneMapping", TONE, A + "curve_max_exposure_increase", "0.0", ParameterStatus.BEDROCK_VALUE,
            "toneMappingMaxExposureIncrease: the exposure alone brightens"),
        new CompatParameter("toneMapping", TONE, A + "curve_shadow_min_slope", "0.38", ParameterStatus.BEDROCK_VALUE,
            "toneMappingShadowMinSlope: the least contrast kept below -2.45 stops"),
        new CompatParameter("toneMapping", TONE, A + "filmic_saturation", "1.0", ParameterStatus.BEDROCK_VALUE,
            "the filmic saturation correction's strength"),
        new CompatParameter("toneMapping", TONE, A + "contrast", "1.0", ParameterStatus.BEDROCK_VALUE,
            "the contrast control, neutral"),
        new CompatParameter("toneMapping", TONE, A + "shadow_contrast", "0.0", ParameterStatus.BEDROCK_VALUE,
            "toneMappingShadowContrast: stops taken off per stop below shadow_contrast_end"),
        new CompatParameter("toneMapping", TONE, A + "shadow_contrast_end", "-4.0", ParameterStatus.BEDROCK_VALUE,
            "toneMappingShadowContrastEnd, in log2 luminance"),
        new CompatParameter("toneMapping", TONE, A + "gamma", "1.0", ParameterStatus.BEDROCK_VALUE,
            "a power on the graded colour before the exact sRGB encoding; 1 leaves the grade neutral"),
        new CompatParameter("toneMapping", TONE, A + "color_balance_r", "1.0", ParameterStatus.BEDROCK_VALUE,
            "the colour balance control, neutral"),
        new CompatParameter("toneMapping", TONE, A + "color_balance_g", "1.0", ParameterStatus.BEDROCK_VALUE,
            "the colour balance control, neutral"),
        new CompatParameter("toneMapping", TONE, A + "color_balance_b", "1.0", ParameterStatus.BEDROCK_VALUE,
            "the colour balance control, neutral"),
        new CompatParameter("toneMapping", TONE, A + "saturation", "1.0", ParameterStatus.BEDROCK_VALUE,
            "Radiante's saturation control on top of the curve; Bedrock has none, so it is neutral"),

        // ---- materials (shader pack attributes, module null). Only behaviour verified from the compiled shader.
        new CompatParameter("materials", null, RT + "albedo_srgb_exact", TRUE, ParameterStatus.MATCHES_BEDROCK,
            "colour textures are decoded with the exact piecewise sRGB curve"),
        new CompatParameter("materials", null, RT + "bedrock_emission", TRUE, ParameterStatus.MATCHES_BEDROCK,
            "emission follows MER green squared; exact up to green 0.25 with converted packs, brighter sources stay "
                + "at full strength (the multiplier is a run-time value, not known)"),

        // ---- transmission
        new CompatParameter("transmission", null, RT + "glass_reflection", "1.0", ParameterStatus.INFERRED,
            "Bedrock does not dim the Fresnel reflection of glass"),
        new CompatParameter("transmission", null, RT + "bedrock_glass_tint", TRUE, ParameterStatus.INFERRED,
            "the view through glass is coloured by colour x (1 - saturate(2 alpha - 1)), the rule Bedrock's sun "
                + "shadow uses (observed)"),
        new CompatParameter("transmission", null, RT + "water_surface_mode", RT + "water_surface_mode.bedrock",
            ParameterStatus.MATCHES_BEDROCK,
            "Bedrock's water normal: three scrolling layers of its water_n texture, faded with the pixel footprint; "
                + "needs a Bedrock install or radiante/bedrock/water_n.tga"),
        new CompatParameter("transmission", null, RT + "bedrock_fresnel", TRUE, ParameterStatus.MATCHES_BEDROCK,
            "glass and water reflectance is Schlick with the cosine remapped to the critical angle; the "
                + "critical-angle cosine is derived from the indices (INFERRED), Bedrock reads it from a table"),
        new CompatParameter("transmission", null, RT + "bedrock_shadow_tint", TRUE, ParameterStatus.MATCHES_BEDROCK,
            "sunlight through a see-through surface = texture colour x (1 - saturate(2 alpha - 1)), shadow when the "
                + "remainder is nearly nothing"),
        // Off: it needs the caustics texture from a Bedrock install, and is subtle at best.
        new CompatParameter("transmission", null, RT + "bedrock_caustics", FALSE, ParameterStatus.MATCHES_BEDROCK,
            "sunlight under water is scaled by 1 + exp(-0.1 depth) (7 c + 0.8 - 1); the repeat size of the pattern "
                + "is a run-time value"),
        new CompatParameter("transmission", null, RT + "bedrock_medium_clamp", TRUE, ParameterStatus.MATCHES_BEDROCK,
            "attenuation inside a medium counts at most 50 blocks of distance"),
        new CompatParameter("transmission", null, RT + "water_density", "1.0", ParameterStatus.INFERRED,
            "Bedrock attenuates the camera's view through water with the full extinction of the medium (observed, "
                + "no thinning factor); needs checking against captures (scene J3)"),

        // ---- fog: Bedrock's volumetric fog in place of every other fog of the air
        new CompatParameter("fog", null, RT + "volumetric_light_mode", RT + "volumetric_light_mode.volumetric",
            ParameterStatus.MATCHES_BEDROCK, "Bedrock fogs the air with a froxel volume"),
        new CompatParameter("fog", null, RT + "froxel_fog", TRUE, ParameterStatus.MATCHES_BEDROCK,
            "Bedrock lights a froxel grid; Radiante lights grids fixed to the world, and marches them per pixel"),
        new CompatParameter("fog", null, RT + "bedrock_volumetric_fog", TRUE, ParameterStatus.MATCHES_BEDROCK,
            "50-block volume with 5 (11^w - 1) slices, the pack's air medium and height profile, sun through shadow "
                + "rays with 1/pi Henyey-Greenstein, sky light; replaces the biome haze, light shafts and Radiante's "
                + "air"),
        new CompatParameter("fog", null, RT + "bedrock_fog_anisotropy", "0.4", ParameterStatus.BEDROCK_VALUE,
            "fogHenyeyGreensteinG, 0.4 in Bedrock RTX"),

        // ---- bloom
        new CompatParameter("bloom", TONE, A + "bloom_enable", TRUE, ParameterStatus.INFERRED,
            "Bedrock RTX has a bloom stage; whether it is always on is not known"),
        new CompatParameter("bloom", TONE, A + "bloom_intensity", "0.01", ParameterStatus.BEDROCK_VALUE,
            "gBloomMultiplier, on a bloom of Radiante's own shape"),
        new CompatParameter("bloom", TONE, A + "bloom_scatter", "0.5", ParameterStatus.APPROXIMATE,
            "how much of the wider levels is carried back up; the filter shape is a placeholder"),
        new CompatParameter("bloom", TONE, A + "bloom_threshold", "0.0", ParameterStatus.UNKNOWN,
            "no threshold uniform is visible in the Bloom material; 0 keeps everything"),
        new CompatParameter("bloom", TONE, A + "bloom_weight_1", "1.0", ParameterStatus.UNKNOWN,
            "per-level weight, finest; " + NOT_MEASURED),
        new CompatParameter("bloom", TONE, A + "bloom_weight_2", "1.0", ParameterStatus.UNKNOWN,
            "per-level weight; " + NOT_MEASURED),
        new CompatParameter("bloom", TONE, A + "bloom_weight_3", "1.0", ParameterStatus.UNKNOWN,
            "per-level weight; " + NOT_MEASURED),
        new CompatParameter("bloom", TONE, A + "bloom_weight_4", "1.0", ParameterStatus.UNKNOWN,
            "per-level weight, widest; " + NOT_MEASURED));

    private BedrockRtxValues() {
    }
}
