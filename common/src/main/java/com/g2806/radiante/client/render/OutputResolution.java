package com.g2806.radiante.client.render;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The size the world pipeline renders and upscales to, below the window's: the world is traced, denoised, upscaled
 * and post processed at this size, and a final spatial upscale (FSR 1) brings it to the window, where Minecraft draws
 * its HUD and menus at full resolution on top. Fixed heights keep the window's aspect ratio; Custom scales both sides
 * by a percentage of the player's choosing. A size at or above the window's is the window's own (Native).
 */
public enum OutputResolution {
    NATIVE(0),
    P1440(1440),
    P1080(1080),
    P900(900),
    P720(720),
    CUSTOM(0);

    /** The lowest and highest share of the window, in percent, the Custom size can be set to. */
    public static final int MIN_PERCENT = 25;
    public static final int MAX_PERCENT = 99;
    /** The Custom share before the player has moved its slider. */
    public static final int DEFAULT_PERCENT = 75;

    /** The output height, or 0 for Native and Custom. */
    public final int height;

    OutputResolution(int height) {
        this.height = height;
    }

    public String key() {
        return "options.radiante.output_resolution." + name().toLowerCase(Locale.ROOT);
    }

    /**
     * The saved name back to a value; anything unknown is Native. The fixed percentages of before (PERCENT_85 and
     * the like) are Custom now; {@link #percentOf} reads their share.
     */
    public static OutputResolution byName(String name) {
        for (OutputResolution value : values()) {
            if (value.name().equals(name)) {
                return value;
            }
        }
        return percentOf(name) > 0 ? CUSTOM : NATIVE;
    }

    /** The share named by a saved value of the old PERCENT_n kind, clamped to the Custom range; 0 for any other. */
    public static int percentOf(String name) {
        if (name == null || !name.startsWith("PERCENT_")) {
            return 0;
        }
        try {
            return clampPercent(Integer.parseInt(name.substring("PERCENT_".length())));
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    public static int clampPercent(int percent) {
        return Math.max(MIN_PERCENT, Math.min(MAX_PERCENT, percent));
    }

    /**
     * The output size for a window of width x height, as {width, height}, with percent the Custom share; the
     * window's own when this is Native or would not be smaller. Sides are kept even, which the upscalers' quality
     * modes divide cleanly.
     */
    public int[] outputSize(int width, int height, int percent) {
        double scale = this == CUSTOM ? clampPercent(percent) / 100.0
            : this.height > 0 && height > 0 ? (double) this.height / height : 1.0;
        if (scale >= 1.0 || width <= 0 || height <= 0) {
            return new int[] {width, height};
        }
        int outWidth = Math.max(2, (int) Math.round(width * scale) & ~1);
        int outHeight = Math.max(2, this.height > 0 ? this.height : (int) Math.round(height * scale) & ~1);
        if (outWidth >= width || outHeight >= height) {
            return new int[] {width, height};
        }
        return new int[] {outWidth, outHeight};
    }

    /**
     * The values worth offering for a window of width x height: Native, the fixed heights that make the world
     * smaller, and Custom, whose share always does.
     */
    public static List<OutputResolution> offeredFor(int width, int height) {
        List<OutputResolution> offered = new ArrayList<>();
        for (OutputResolution value : values()) {
            int[] size = value.outputSize(width, height, MAX_PERCENT);
            if (value == NATIVE || value == CUSTOM || size[0] != width || size[1] != height) {
                offered.add(value);
            }
        }
        return offered;
    }
}
