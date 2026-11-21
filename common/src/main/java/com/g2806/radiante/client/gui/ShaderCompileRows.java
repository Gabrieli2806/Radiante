package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.pipeline.PipelineSettings;
import com.g2806.radiante.client.pipeline.RtAttributes;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Which rows of the settings compile shaders when changed: those of a shader pack attribute that is compiled into the
 * shaders rather than read live ("runtime": false in the pack's configs.json, AttributeConfig.runtime), so the change
 * waits for the shaders it touches to compile. The screen marks them with a small icon and says so under the
 * description (SettingsLayoutDraw). Read from the pack each time, so a setting a pack makes live loses its mark, and
 * a pack's own compiled settings on its page carry it as Radiante PT's do.
 */
final class ShaderCompileRows {

    private static final String RAY_TRACING_ATTRIBUTE = "render_pipeline.module.ray_tracing.attribute.";

    /** Rows whose key is not their attribute's name: the option's key to the attribute it sets. */
    private static final Map<String, String> BY_KEY = new ConcurrentHashMap<>(Map.ofEntries(
        Map.entry("options.radiante.ray_bounces", RtAttributes.RAY_BOUNCES_ATTRIBUTE),
        Map.entry("options.radiante.parallax", RtAttributes.PARALLAX_ATTRIBUTE),
        Map.entry("options.radiante.atmosphere_style", RtAttributes.BEDROCK_ATMOSPHERE_ATTRIBUTE),
        Map.entry("options.radiante.volumetric_fog_samples", RtAttributes.VOLUMETRIC_SAMPLES_ATTRIBUTE),
        Map.entry("options.radiante.motion_blur", RtAttributes.MOTION_BLUR_ATTRIBUTE),
        Map.entry("options.radiante.depth_of_field", RtAttributes.DEPTH_OF_FIELD_ATTRIBUTE),
        Map.entry("options.radiante.fog_style", RtAttributes.FROXEL_FOG_ATTRIBUTE),
        Map.entry("options.radiante.cloud_shadows", RtAttributes.CLOUD_SHADOWS_ATTRIBUTE),
        Map.entry("options.radiante.seamless_glass", RtAttributes.SEAMLESS_GLASS_ATTRIBUTE),
        Map.entry("options.radiante.restir", RtAttributes.RESTIR_ATTRIBUTE),
        Map.entry("options.radiante.restir_gi", RtAttributes.RESTIR_GI_ATTRIBUTE)));

    private ShaderCompileRows() {
    }

    /** A row built for a shader pack attribute (a slider or a switch of the pack's): marked by what it sets. */
    static void register(String optionKey, String attribute) {
        if (optionKey != null && attribute != null) {
            BY_KEY.put(optionKey, attribute);
        }
    }

    /** Whether changing the row with this key compiles shaders. */
    static boolean compiles(String optionKey) {
        if (optionKey == null) {
            return false;
        }
        // A debug view is compiled in: each one is its own build of the shaders.
        if (optionKey.equals("options.radiante.debug_view")) {
            return true;
        }
        String attribute = BY_KEY.get(optionKey);
        if (attribute == null && optionKey.startsWith(RAY_TRACING_ATTRIBUTE)) {
            // The Advanced rows and a pack's page are keyed by the attribute's own name; a colour's three sliders by
            // the name and a letter.
            attribute = optionKey;
            if (!PipelineSettings.hasShaderPackAttribute(attribute) && attribute.matches(".*\\.[a-z]$")) {
                attribute = attribute.substring(0, attribute.length() - 2);
            }
        }
        return attribute != null && PipelineSettings.compilesShadersOnChange(attribute);
    }
}
