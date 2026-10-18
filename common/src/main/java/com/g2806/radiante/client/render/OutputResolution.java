package com.g2806.radiante.client.render;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The size the world pipeline renders and upscales to, below the window's: the world is traced, denoised, upscaled
 * and post processed at this size, and a final spatial upscale (FSR 1) brings it to the window, where Minecraft draws
 * its HUD and menus at full resolution on top. Fixed heights keep the window's aspect ratio; percentages scale both
 * sides. A size at or above the window's is the window's own (Native).
 */
public enum OutputResolution {
    NATIVE(0, 0),
    P1440(1440, 0),
    P1080(1080, 0),
    P900(900, 0),
    P720(720, 0),
    PERCENT_85(0, 85),
    PERCENT_75(0, 75),
    PERCENT_67(0, 67),
    PERCENT_50(0, 50);

    /** The output height, or 0 for a percentage. */
    public final int height;
    /** The share of the window, in percent, or 0 for a fixed height. */
    public final int percent;

    OutputResolution(int height, int percent) {
        this.height = height;
        this.percent = percent;
    }

    public String key() {
        return "options.radiante.output_resolution." + name().toLowerCase(Locale.ROOT);
    }

    /** The saved name back to a value; anything unknown is Native. */
    public static OutputResolution byName(String name) {
        for (OutputResolution value : values()) {
            if (value.name().equals(name)) {
                return value;
            }
        }
        return NATIVE;
    }

    /**
     * The output size for a window of width x height, as {width, height}; the window's own when this is Native or
     * would not be smaller. Sides are kept even, which the upscalers' quality modes divide cleanly.
     */
    public int[] outputSize(int width, int height) {
        double scale = this.percent > 0 ? this.percent / 100.0
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

    /** Whether this renders below a window of width x height at all. */
    public boolean reduces(int width, int height) {
        int[] size = outputSize(width, height);
        return size[0] != width || size[1] != height;
    }

    /** The values worth offering for a window of width x height: Native and those that make the world smaller. */
    public static List<OutputResolution> offeredFor(int width, int height) {
        List<OutputResolution> offered = new ArrayList<>();
        for (OutputResolution value : values()) {
            if (value == NATIVE || value.reduces(width, height)) {
                offered.add(value);
            }
        }
        return offered;
    }
}
