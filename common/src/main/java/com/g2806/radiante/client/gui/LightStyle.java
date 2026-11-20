package com.g2806.radiante.client.gui;

import java.util.Map;
import java.util.Objects;

/**
 * How bright the picture is held, as one choice over two sliders: Exposure and Local Tone Mapping. Changing either by
 * hand shows as Custom.
 * <ul>
 *   <li>Bright, for play: seven tenths of a stop up, and the dark parts of a picture lifted more, so caves and rooms
 *   read clearly.</li>
 *   <li>Balanced, the default: the sliders' own defaults.</li>
 *   <li>Cinematic: a stop down and little lifting, so a cave stays dark round its lights and a bright patch (lava, a
 *   window) is not evened out with the rest. The eye already closes for the light lava throws round itself and opens
 *   again looking away; here that shows plainly.</li>
 * </ul>
 * (A third slider, Glare, which was to let the glow in view close the eye too, changed nothing that could be measured:
 * looking at lava already closes it by two stops through the light lava casts, which the meter reads.)
 */
enum LightStyle {
    BRIGHT("options.radiante.light_style.bright", 7, 65),
    BALANCED("options.radiante.light_style.balanced", 0, 40),
    CINEMATIC("options.radiante.light_style.cinematic", -10, 10),
    CUSTOM("options.radiante.light_style.custom", 0, 0);

    final String key;
    /** Exposure in steps of a tenth of a stop, Local Tone Mapping in percent (the sliders' steps). */
    final int exposure;
    final int localTone;

    LightStyle(String key, int exposure, int localTone) {
        this.key = key;
        this.exposure = exposure;
        this.localTone = localTone;
    }

    boolean matches(Map<Tunable, Integer> values) {
        return this != CUSTOM && same(values.get(Tunable.EXPOSURE_BIAS), this.exposure)
            && same(values.get(Tunable.LOCAL_TONE_MAPPING), this.localTone);
    }

    private static boolean same(Integer value, int expected) {
        return value == null || Objects.equals(value, expected);
    }

    void applyTo(Map<Tunable, Integer> values) {
        values.computeIfPresent(Tunable.EXPOSURE_BIAS, (k, v) -> this.exposure);
        values.computeIfPresent(Tunable.LOCAL_TONE_MAPPING, (k, v) -> this.localTone);
    }

    static LightStyle of(Map<Tunable, Integer> values) {
        for (LightStyle style : values()) {
            if (style.matches(values)) {
                return style;
            }
        }
        return CUSTOM;
    }
}
