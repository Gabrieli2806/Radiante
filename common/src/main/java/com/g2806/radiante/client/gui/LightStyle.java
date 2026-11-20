package com.g2806.radiante.client.gui;

import java.util.Map;
import java.util.Objects;

/**
 * How the picture is held, as one choice over five sliders: Exposure, Local Tone Mapping, Brightness, Contrast and
 * Shadow Depth. Changing any of them by hand shows as Custom.
 * <ul>
 *   <li>Bright, for play: brighter and evened out, so caves and rooms read clearly.</li>
 *   <li>Balanced: the look of before, an even picture.</li>
 *   <li>Cinematic, the default: a little darker, crisper and deeper in its shadows, with little lifting of the dark
 *   parts, so a cave stays dark round its lights and a bright patch (lava, a window) is not evened out with the rest.
 *   The eye closes for the light lava throws round itself and opens again looking away.</li>
 * </ul>
 * Exposure goes in before Bedrock's tone curve, which takes most of it back; Brightness after it, where it shows.
 */
enum LightStyle {
    BRIGHT("options.radiante.light_style.bright", 3, 55, 3, 100, 0),
    BALANCED("options.radiante.light_style.balanced", 0, 40, 1, 100, 0),
    CINEMATIC("options.radiante.light_style.cinematic", -5, 15, -3, 106, 12),
    CUSTOM("options.radiante.light_style.custom", 0, 0, 0, 0, 0);

    final String key;
    /** Each slider's value in its steps (Tunable): Exposure, Local Tone Mapping, Brightness, Contrast, Shadow Depth. */
    private final Map<Tunable, Integer> values;

    LightStyle(String key, int exposure, int localTone, int brightness, int contrast, int shadowDepth) {
        this.key = key;
        this.values = Map.of(Tunable.EXPOSURE_BIAS, exposure, Tunable.LOCAL_TONE_MAPPING, localTone,
            Tunable.TONE_BRIGHTNESS, brightness, Tunable.CONTRAST, contrast, Tunable.SHADOW_DEPTH, shadowDepth);
    }

    boolean matches(Map<Tunable, Integer> current) {
        if (this == CUSTOM) {
            return false;
        }
        for (Map.Entry<Tunable, Integer> entry : this.values.entrySet()) {
            Integer value = current.get(entry.getKey());
            if (value != null && !Objects.equals(value, entry.getValue())) {
                return false;
            }
        }
        return true;
    }

    void applyTo(Map<Tunable, Integer> current) {
        this.values.forEach((tunable, value) -> current.computeIfPresent(tunable, (k, v) -> value));
    }

    static LightStyle of(Map<Tunable, Integer> current) {
        for (LightStyle style : values()) {
            if (style.matches(current)) {
                return style;
            }
        }
        return CUSTOM;
    }
}
