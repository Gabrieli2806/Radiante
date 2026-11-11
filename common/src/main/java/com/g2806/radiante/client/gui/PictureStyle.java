package com.g2806.radiante.client.gui;

import java.util.Map;
import java.util.Objects;

/**
 * The look of the image as a whole: how the colours are mapped to the screen and how saturated they are. Changing
 * either by hand shows as Custom.
 */
enum PictureStyle {
    NATURAL("options.radiante.picture_style.natural", ToneMethod.PBR_NEUTRAL, 100),
    BEDROCK("options.radiante.picture_style.bedrock", ToneMethod.BEDROCK, 100),
    VIVID("options.radiante.picture_style.vivid", ToneMethod.ACES, 130),
    CUSTOM("options.radiante.picture_style.custom", null, 0);

    final String key;
    final ToneMethod method;
    final int saturation;

    PictureStyle(String key, ToneMethod method, int saturation) {
        this.key = key;
        this.method = method;
        this.saturation = saturation;
    }

    boolean matches(ToneMethod currentMethod, Map<Tunable, Integer> values) {
        return this != CUSTOM && (currentMethod == null || currentMethod == this.method)
            && same(values.get(Tunable.SATURATION), this.saturation);
    }

    private static boolean same(Integer value, int expected) {
        return value == null || Objects.equals(value, expected);
    }

    void applyTo(Map<Tunable, Integer> values) {
        values.computeIfPresent(Tunable.SATURATION, (k, v) -> this.saturation);
    }

    /** The tone mapping curves offered; the pipeline has a few more, shown as they are when chosen elsewhere. */
    enum ToneMethod {
        // Bedrock RTX's histogram curve, the default.
        BEDROCK("bedrock_provisional"),
        PBR_NEUTRAL("pbr_neutral"),
        ACES("aces"),
        REINHARD("reinhard");

        static final String PREFIX = "render_pipeline.module.tone_mapping.attribute.method.";
        final String value;

        ToneMethod(String suffix) {
            this.value = PREFIX + suffix;
        }

        String key() {
            return "options.radiante.tone_method." + name().toLowerCase(java.util.Locale.ROOT);
        }

        static ToneMethod of(String value) {
            for (ToneMethod method : values()) {
                if (method.value.equals(value)) {
                    return method;
                }
            }
            return null;
        }
    }
}
