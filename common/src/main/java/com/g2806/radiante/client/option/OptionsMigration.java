package com.g2806.radiante.client.option;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.bedrock.BedrockRtxValues;
import com.g2806.radiante.client.bedrock.CompatParameter;
import com.g2806.radiante.client.pipeline.Pipeline;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Settings saved by an older version that need putting right once.
 *
 * <p>Versions before this one had renderer profiles (Bedrock RTX (Enhanced), Realistic, Custom), saved as
 * {@code rendererProfile} in options.properties. Enhanced wrote its values into the pipeline on every build; they are
 * radiante-pt's plain defaults now ({@link BedrockRtxValues}), so a player who had it (or no saved profile) has them
 * already and nothing changes. Realistic and Custom had other values in the settings a profile managed: those are put
 * back to the defaults, once, and every other setting is left as the player set it. The key itself is dropped, since
 * options.properties is written from the options this version knows.
 */
public final class OptionsMigration {

    /** The options.properties key the old profile was saved under. */
    static final String PROFILE_KEY = "rendererProfile";

    private static final String RT = "render_pipeline.module.ray_tracing.attribute.";
    private static final String A = "render_pipeline.module.tone_mapping.attribute.";

    /**
     * The settings only the Realistic profile set, at their defaults: the rest of what it set is in
     * {@link BedrockRtxValues}. Kept here because a Realistic player's values for them were the profile's, not theirs.
     */
    static final List<CompatParameter> REALISTIC_ONLY = List.of(
        realisticOnly(null, RT + "bounce_light_boost", "1.5"),
        realisticOnly(null, RT + "sky_light_boost", "1.8"),
        realisticOnly(null, RT + "exposure_day_scale", "0.48"),
        realisticOnly(null, RT + "light_shafts", "0.015"),
        realisticOnly(null, RT + "sun_glow", "0.08"),
        realisticOnly(null, RT + "mirror_max_roughness", "0.03"),
        realisticOnly(null, RT + "water_wave_strength", "0.35"),
        realisticOnly(null, RT + "water_clear_teal", "0.0"),
        realisticOnly(Pipeline.TONE_MAPPING_MODULE_NAME, A + "exposure_up_speed", "8.0"),
        realisticOnly(Pipeline.TONE_MAPPING_MODULE_NAME, A + "exposure_down_speed", "8.0"));

    /** The profile read from options.properties at start-up and not yet dealt with; null when there is none. */
    private static String pendingProfile;

    private OptionsMigration() {
    }

    private static CompatParameter realisticOnly(String module, String attribute, String value) {
        return new CompatParameter("realistic", module, attribute, value, null, "the default");
    }

    /** Remembers the saved profile until the pipeline is loaded (Options.readOptions). */
    static void rememberProfile(String stored) {
        pendingProfile = stored;
    }

    /**
     * The settings to put back to their defaults for a saved profile: none for Enhanced, a missing value or any name
     * this version does not know (older versions' profiles read as Enhanced, as they did before).
     */
    public static List<CompatParameter> settingsToReset(String storedProfile) {
        String name = storedProfile == null ? "" : storedProfile.trim().toUpperCase(Locale.ROOT);
        List<CompatParameter> reset = new ArrayList<>();
        if (name.equals("REALISTIC") || name.equals("CUSTOM")) {
            reset.addAll(BedrockRtxValues.PARAMETERS);
        }
        if (name.equals("REALISTIC")) {
            reset.addAll(REALISTIC_ONLY);
        }
        return reset;
    }

    /** Once the pipeline is loaded: puts back what the saved profile needs, then forgets it. */
    public static void applyAfterPipelineLoad() {
        String stored = pendingProfile;
        pendingProfile = null;
        List<CompatParameter> reset = settingsToReset(stored);
        if (reset.isEmpty()) {
            return;
        }
        boolean changed = false;
        for (CompatParameter parameter : reset) {
            changed |= parameter.module() == null ? Pipeline.setShaderPackValue(parameter.attribute(), parameter.value())
                : Pipeline.setModuleValue(parameter.module(), parameter.attribute(), parameter.value());
        }
        if (changed) {
            Pipeline.savePipeline();
        }
        RadianteClient.LOGGER.info("Renderer profiles are gone: the settings the {} profile set are back to "
            + "radiante-pt's defaults, the Bedrock RTX look (values changed: {}); other settings are kept", stored,
            changed);
    }
}
