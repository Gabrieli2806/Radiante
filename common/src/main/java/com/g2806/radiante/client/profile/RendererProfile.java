package com.g2806.radiante.client.profile;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.Pipeline;

import java.util.Locale;
import java.util.Map;

/**
 * A named set of renderer settings.
 *
 * <ul>
 * <li>{@link #BEDROCK_RTX_ENHANCED}: the main profile. Bedrock RTX's own look (its sky and sunlight, its exposure and
 * tone mapping as read from the running game, its materials, water, glass and fog; see {@link BedrockRtxProfile})
 * with a few things on top that look better than Bedrock RTX; what Bedrock does is kept everywhere else. Today:
 * light blocks whose pack gives them little glow look lit by their own light, and the end portal glows.
 * <li>{@link #CUSTOM}: the profile's values with some of them changed by hand. It writes nothing and holds nothing:
 * every attribute stays as it is. The settings screen turns the profile into this the moment one of the settings it
 * manages is changed, so the change is kept instead of being written over on the next build.
 * </ul>
 *
 * Earlier versions had more profiles (Radiante's own look, a first Bedrock approximation, Bedrock RTX without the
 * extras); a saved choice naming one of them reads as {@link #BEDROCK_RTX_ENHANCED}.
 *
 * Applying a profile writes the attributes it manages (see {@link BedrockRtxProfile#PARAMETERS}) and remembers the
 * choice in {@link Options#rendererProfile}; attributes it does not manage are left as the player set them.
 */
public enum RendererProfile {
    BEDROCK_RTX_ENHANCED,
    CUSTOM;

    /** The profile saved in the options; {@link #BEDROCK_RTX_ENHANCED} when it is missing, unknown or removed. */
    public static RendererProfile current() {
        return of(Options.rendererProfile);
    }

    public static RendererProfile of(String name) {
        if (name != null && name.trim().equalsIgnoreCase(CUSTOM.name())) {
            return CUSTOM;
        }
        return BEDROCK_RTX_ENHANCED;
    }

    /** Whether the profile writes the Bedrock settings (everything but Custom). */
    public boolean isBedrock() {
        return this != CUSTOM;
    }

    private static final String ORIGINAL_ATTRIBUTE = Pipeline.BEDROCK_ORIGINAL_ATTRIBUTE;
    private static final String ENHANCED_ATTRIBUTE = Pipeline.BEDROCK_ENHANCED_ATTRIBUTE;
    private static final String TRUE = "render_pipeline.true";
    private static final String FALSE = "render_pipeline.false";

    /**
     * What the profile sets besides the shared Bedrock table: Bedrock RTX's own mode switch; the
     * pack's own metalness and roughness, which its specular response is made for; Bedrock's sky and sunlight; both
     * branches of water and glass traced (full-resolution DLSS guides do not describe alternating missing lobes);
     * and glass as the pack draws it, each block in its frame, as Bedrock shows it.
     */
    private static final Map<String, String> ORIGINAL_VALUES = Map.of(
        ORIGINAL_ATTRIBUTE, TRUE,
        Pipeline.BEDROCK_MATERIALS_ATTRIBUTE, TRUE,
        Pipeline.BEDROCK_ATMOSPHERE_ATTRIBUTE, TRUE,
        Pipeline.TRANSPARENT_CHECKERBOARD_ATTRIBUTE, FALSE,
        Pipeline.SEAMLESS_GLASS_ATTRIBUTE, FALSE,
        Pipeline.FACE_SHADING_ATTRIBUTE, FALSE);

    private static final String TONE = "render_pipeline.module.tone_mapping.attribute.";

    /**
     * The profile's exposure and tone mapping: Bedrock RTX's own values, read from the running game
     * (its frame constants and its tone mapping material's uniforms, the same at every time of day). They take the
     * place of the table's older stand-ins ({@link BedrockRtxProfile}); the exposure range and the speed it
     * adapts at are the metering mode's own (tone_mapping/exposure.comp). The brightness bias and the saturation
     * are Radiante's controls on top of the curve; Bedrock has neither, so they are held neutral.
     */
    private static final Map<String, String> ORIGINAL_TONE_VALUES = Map.ofEntries(
        Map.entry(TONE + "exposure_metering_mode", TONE + "exposure_metering_mode.bedrock_original"),
        Map.entry(TONE + "curve_dynamic_range", "15.0"),
        Map.entry(TONE + "curve_shift", "0.05"),
        Map.entry(TONE + "curve_max_exposure_increase", "0.0"),
        Map.entry(TONE + "curve_shadow_min_slope", "0.38"),
        Map.entry(TONE + "shadow_contrast", "0.0"),
        Map.entry(TONE + "shadow_contrast_end", "-4.0"),
        Map.entry(TONE + "filmic_saturation", "1.0"),
        Map.entry(TONE + "contrast", "1.0"),
        Map.entry(TONE + "gamma", "1.0"),
        Map.entry(TONE + "color_balance_r", "1.0"),
        Map.entry(TONE + "color_balance_g", "1.0"),
        Map.entry(TONE + "color_balance_b", "1.0"),
        Map.entry(TONE + "bloom_intensity", "0.01"),
        Map.entry(TONE + "exposure_bias", "0.0"),
        Map.entry(TONE + "saturation", "1.0"));

    /**
     * The value this profile writes into an attribute on every build, or null when it leaves the attribute to the
     * player. The settings screen asks, so that a setting changed against it turns the profile into Custom instead
     * of being written over without a word.
     */
    public String managedValue(String attribute) {
        if (isBedrock() && attribute.equals(ENHANCED_ATTRIBUTE)) {
            return TRUE;
        }
        if (isBedrock() && ORIGINAL_VALUES.containsKey(attribute)) {
            return ORIGINAL_VALUES.get(attribute);
        }
        if (isBedrock() && ORIGINAL_TONE_VALUES.containsKey(attribute)) {
            return ORIGINAL_TONE_VALUES.get(attribute);
        }
        if (isBedrock()) {
            for (CompatParameter parameter : BedrockRtxProfile.PARAMETERS) {
                if (parameter.attribute().equals(attribute)) {
                    return valueFor(parameter);
                }
            }
        }
        return null;
    }

    private String valueFor(CompatParameter parameter) {
        if (isBedrock() && ORIGINAL_TONE_VALUES.containsKey(parameter.attribute())) {
            return ORIGINAL_TONE_VALUES.get(parameter.attribute());
        }
        return parameter.bedrockValue();
    }

    /** The tone values that no table parameter covers, written alongside them. */
    private boolean setOriginalToneExtras() {
        if (!isBedrock()) {
            return false;
        }
        boolean changed = false;
        for (Map.Entry<String, String> value : ORIGINAL_TONE_VALUES.entrySet()) {
            boolean shared = BedrockRtxProfile.PARAMETERS.stream().anyMatch(p -> p.attribute().equals(value.getKey()));
            if (!shared) {
                changed |= Pipeline.setModuleValue(Pipeline.TONE_MAPPING_MODULE_NAME, value.getKey(), value.getValue());
            }
        }
        return changed;
    }

    /**
     * Bedrock RTX's mode switch and what it needs with it, and the enhanced extras' switch on top. Custom leaves
     * them as they are, like everything else.
     */
    private boolean setOriginalMode() {
        if (this == CUSTOM) {
            return false;
        }
        boolean changed = Pipeline.setShaderPackValue(ENHANCED_ATTRIBUTE, TRUE);
        for (Map.Entry<String, String> value : ORIGINAL_VALUES.entrySet()) {
            changed |= Pipeline.setShaderPackValue(value.getKey(), value.getValue());
        }
        return changed;
    }

    /** Reapplies managed values when the pipeline is rebuilt or loaded. */
    public void enforce() {
        setOriginalMode();
        if (!isBedrock()) {
            return;
        }
        setOriginalToneExtras();
        for (CompatParameter parameter : BedrockRtxProfile.PARAMETERS) {
            if (parameter.module() == null) {
                Pipeline.setShaderPackValue(parameter.attribute(), valueFor(parameter));
            } else {
                Pipeline.setModuleValue(parameter.module(), parameter.attribute(), valueFor(parameter));
            }
        }
    }

    /**
     * Writes the profile's values and remembers the choice; true when a value changed. Whether that takes a rebuild
     * is the caller's to ask (Pipeline.takeStructuralChange, Pipeline.pushRuntimeChanges).
     */
    public boolean apply() {
        if (this == CUSTOM) {
            Options.rendererProfile = name();
            Options.overwriteConfig();
            RadianteClient.LOGGER.info("Renderer profile {}: values kept as they are", this);
            return false;
        }
        boolean changed = setOriginalMode();
        changed |= setOriginalToneExtras();
        for (CompatParameter parameter : BedrockRtxProfile.PARAMETERS) {
            String value = valueFor(parameter);
            boolean shaderPack = parameter.module() == null;
            boolean written = shaderPack ? Pipeline.setShaderPackValue(parameter.attribute(), value)
                : Pipeline.setModuleValue(parameter.module(), parameter.attribute(), value);
            String present = shaderPack ? Pipeline.getShaderPackValue(parameter.attribute())
                : Pipeline.getModuleValue(parameter.module(), parameter.attribute());
            if (!written && present == null) {
                RadianteClient.LOGGER.warn("Profile {}: the pipeline has no attribute {} in {}", this,
                    parameter.attribute(), parameter.module());
            }
            changed |= written;
        }
        Options.rendererProfile = name();
        Options.overwriteConfig();
        if (changed) {
            Pipeline.savePipeline();
        }
        RadianteClient.LOGGER.info("Renderer profile {} applied (values changed: {})", this, changed);
        return changed;
    }
}
