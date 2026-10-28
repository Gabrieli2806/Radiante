package com.g2806.radiante.client.profile;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.Pipeline;

import java.util.Map;

/**
 * A named set of renderer settings.
 *
 * <ul>
 * <li>{@link #BEDROCK_RTX_ENHANCED}: the main profile. Bedrock RTX's own look (its sky and sunlight, its exposure and
 * tone mapping as calibrated to Bedrock RTX, its materials, water, glass and fog; see {@link BedrockRtxProfile})
 * with a few things on top that look better than Bedrock RTX; what Bedrock does is kept everywhere else. Today:
 * light blocks whose pack gives them little glow look lit by their own light, and the end portal glows.
 * <li>{@link #REALISTIC}: the Enhanced profile (so the Bedrock materials, exposure and fog machinery are all in
 * place) with the settings that make a picture read as plastic or toy-like set the other way: less ambient and bounce
 * fill, a steeper tone curve, calmer colour and bloom, rougher-looking water and glass. See {@link #REALISTIC_VALUES}
 * for every value and the reason for it.
 * <li>{@link #CUSTOM}: the profile's values with some of them changed by hand. It writes nothing and holds nothing:
 * every attribute stays as it is. The settings screen turns the profile into this the moment one of the settings it
 * manages is changed, so the change is kept instead of being written over on the next build.
 * </ul>
 *
 * Earlier versions had more profiles (Radiante's own look, a first Bedrock approximation, Bedrock RTX without the
 * extras); a saved choice naming one of them reads as {@link #BEDROCK_RTX_ENHANCED}.
 *
 * Applying a profile writes the attributes it manages (see {@link BedrockRtxProfile#PARAMETERS}) and remembers the
 * choice in {@link Options#rendererProfile}; attributes it does not manage are left as the player set them. With a
 * shader pack other than Radiante PT, it writes the pack's attributes only where the pack lists them in
 * {@code "profile_keys"} ({@link Pipeline#profileMayWrite}).
 */
public enum RendererProfile {
    BEDROCK_RTX_ENHANCED,
    REALISTIC,
    CUSTOM;

    /** The profile saved in the options; {@link #BEDROCK_RTX_ENHANCED} when it is missing, unknown or removed. */
    public static RendererProfile current() {
        return of(Options.rendererProfile);
    }

    public static RendererProfile of(String name) {
        if (name != null && name.trim().equalsIgnoreCase(CUSTOM.name())) {
            return CUSTOM;
        }
        if (name != null && name.trim().equalsIgnoreCase(REALISTIC.name())) {
            return REALISTIC;
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

    private static final String RT = "render_pipeline.module.ray_tracing.attribute.";

    /**
     * What {@link #REALISTIC} sets on top of the Enhanced profile: the attribute, the value, the value it has under
     * the Enhanced profile when that profile does not set it itself (so that going back to Enhanced restores it; the
     * shader pack's or the module's default), and why. An attribute the Enhanced profile does set takes the
     * realistic value instead of its own.
     */
    public record Override(String attribute, String value, String enhanced, String reason) {
    }

    /** See {@link Override}. */
    public static final java.util.List<Override> REALISTIC_VALUES = java.util.List.of(
        // ---- light: less fill, so shadows keep their depth and a lit room falls off the way light does
        new Override(RT + "bounce_light_boost", "0.8", "1.5",
            "bounced light below the default's lift: the default fills every shadow like a second light"),
        new Override(RT + "sky_light_boost", "1.0", "1.8",
            "the sky's light at its own strength: the default adds an even blue-white ambient that flattens shade"),
        new Override(RT + "exposure_day_scale", "0.42", "0.48",
            "a touch less daylight brightness, so skies and snow keep detail instead of sitting near white"),
        new Override(RT + "light_shafts", "0.008", "0.015",
            "half the light shafts: haze that glows everywhere reads as a soft-focus filter"),
        new Override(RT + "sun_glow", "0.04", "0.08",
            "half the glow around the sun, which blooms into a milky halo"),
        // ---- materials and water
        new Override(RT + "mirror_max_roughness", "0.02", "0.03",
            "only truly polished metal is a clean mirror; slightly rough metal reads as metal, not chrome"),
        new Override(RT + "water_surface_mode", RT + "water_surface_mode.realistic",
            RT + "water_surface_mode.bedrock",
            "finer, irregular waves instead of Bedrock's magnified water texture, whose pixels show at grazing angles"),
        new Override(RT + "water_wave_strength", "0.5", "0.35",
            "waves strong enough to break the mirror into ripples"),
        new Override(RT + "water_clear_teal", "0.7", "0.0",
            "water that absorbs red first and stays clear in green and blue: a teal you see the bottom through, "
                + "where the pack's water closes in as a deep blue murk"),
        // ---- tone: Bedrock's filmic curve with more contrast, calm colour, no glow, an eye that adapts like one
        new Override(TONE + "contrast", "1.08", "1.0",
            "more separation between light and dark about middle grey"),
        new Override(TONE + "saturation", "0.92", "1.0",
            "colour a little below the textures' own, which look like toy plastic at full strength"),
        new Override(TONE + "bloom_intensity", "0.004", "0.01",
            "bloom almost off: a cinematic glow around everything is the soft, plastic look"),
        new Override(TONE + "exposure_up_speed", "4.0", "8.0",
            "opening up in the dark takes a moment, as an eye does"),
        new Override(TONE + "exposure_down_speed", "6.0", "8.0",
            "closing in the light is quicker than opening in the dark")
    );

    /**
     * The profile's exposure and tone mapping: Bedrock RTX's own values
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
        if (!mayWrite(attribute)) {
            return null;
        }
        Override realistic = realisticOverride(attribute);
        if (realistic != null) {
            return realistic.value();
        }
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

    /** The Realistic profile's row for the attribute while this profile is Realistic; otherwise null. */
    private Override realisticOverride(String attribute) {
        if (this != REALISTIC) {
            return null;
        }
        for (Override row : REALISTIC_VALUES) {
            if (row.attribute().equals(attribute)) {
                return row;
            }
        }
        return null;
    }

    private static boolean isToneAttribute(String attribute) {
        return attribute.startsWith(TONE);
    }

    private static boolean writeAttribute(String attribute, String value) {
        return isToneAttribute(attribute) ? Pipeline.setModuleValue(Pipeline.TONE_MAPPING_MODULE_NAME, attribute, value)
            : writePackValue(attribute, value);
    }

    /**
     * With a shader pack other than Radiante PT, the profile leaves the pack's attributes alone unless the pack
     * lists them in its {@code "profile_keys"} (Pipeline.profileMayWrite): Radiante PT's values are made for Radiante
     * PT, and a pack may give the same name another meaning. Attributes of other modules are always the profile's.
     */
    private static boolean mayWrite(String attribute) {
        return !attribute.startsWith(RT) || Pipeline.profileMayWrite(attribute);
    }

    private static boolean writePackValue(String attribute, String value) {
        return mayWrite(attribute) && Pipeline.setShaderPackValue(attribute, value);
    }

    private static boolean tableHas(String attribute) {
        return BedrockRtxProfile.PARAMETERS.stream().anyMatch(p -> p.attribute().equals(attribute))
            || ORIGINAL_TONE_VALUES.containsKey(attribute) || ORIGINAL_VALUES.containsKey(attribute);
    }

    /**
     * The Realistic rows no table covers (the Enhanced profile leaves them to the player): written here when the
     * profile is Realistic, and set back to their Enhanced value when the profile that replaces Realistic is not.
     */
    private boolean setRealisticExtras(boolean restore) {
        boolean changed = false;
        for (Override row : REALISTIC_VALUES) {
            if (!tableHas(row.attribute())) {
                changed |= writeAttribute(row.attribute(), restore ? row.enhanced() : row.value());
            }
        }
        return changed;
    }

    private String valueFor(CompatParameter parameter) {
        Override realistic = realisticOverride(parameter.attribute());
        if (realistic != null) {
            return realistic.value();
        }
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
                Override realistic = realisticOverride(value.getKey());
                changed |= Pipeline.setModuleValue(Pipeline.TONE_MAPPING_MODULE_NAME, value.getKey(),
                    realistic != null ? realistic.value() : value.getValue());
            }
        }
        if (this == REALISTIC) {
            changed |= setRealisticExtras(false);
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
        boolean changed = writePackValue(ENHANCED_ATTRIBUTE, TRUE);
        for (Map.Entry<String, String> value : ORIGINAL_VALUES.entrySet()) {
            changed |= writePackValue(value.getKey(), value.getValue());
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
                writePackValue(parameter.attribute(), valueFor(parameter));
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
        if (this != REALISTIC && RendererProfile.current() == REALISTIC) {
            changed |= setRealisticExtras(true);
        }
        changed |= setOriginalToneExtras();
        for (CompatParameter parameter : BedrockRtxProfile.PARAMETERS) {
            String value = valueFor(parameter);
            boolean shaderPack = parameter.module() == null;
            boolean written = shaderPack ? writePackValue(parameter.attribute(), value)
                : Pipeline.setModuleValue(parameter.module(), parameter.attribute(), value);
            String present = shaderPack ? Pipeline.getShaderPackValue(parameter.attribute())
                : Pipeline.getModuleValue(parameter.module(), parameter.attribute());
            if (!written && present == null && mayWrite(parameter.attribute())) {
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
