package com.g2806.radiante.client.option;

import java.util.Locale;

/** Linear RGB multipliers, stored in vertices as the standard RGB9_E5 shared-exponent format. */
public record EmissionColor(float red, float green, float blue) {
    public static final float MAX = 65408.0f;
    public static final EmissionColor WHITE = new EmissionColor(1, 1, 1);
    public static final EmissionColor BLACK = new EmissionColor(0, 0, 0);

    public EmissionColor {
        if (!Float.isFinite(red) || !Float.isFinite(green) || !Float.isFinite(blue)) {
            throw new IllegalArgumentException("Emission colour must be finite");
        }
        red = Math.clamp(red, 0, MAX);
        green = Math.clamp(green, 0, MAX);
        blue = Math.clamp(blue, 0, MAX);
    }

    public float peak() { return Math.max(red, Math.max(green, blue)); }
    public EmissionColor multiply(EmissionColor other) { return new EmissionColor(red * other.red, green * other.green, blue * other.blue); }
    public EmissionColor multiply(float value) { return new EmissionColor(red * value, green * value, blue * value); }

    public long packed() {
        float peak = peak();
        if (peak == 0) return 0;
        int exponent = Math.max(0, Math.getExponent(peak) + 16);
        double step = Math.scalb(1.0, exponent - 24);
        if (Math.round(peak / step) == 512) { exponent++; step *= 2; }
        int r = (int) Math.round(red / step), g = (int) Math.round(green / step), b = (int) Math.round(blue / step);
        return Integer.toUnsignedLong(r | g << 9 | b << 18 | exponent << 27);
    }

    public static EmissionColor unpack(long encoded) {
        int word = (int) encoded;
        float step = Math.scalb(1.0f, (word >>> 27) - 24);
        return new EmissionColor((word & 511) * step, ((word >>> 9) & 511) * step, ((word >>> 18) & 511) * step);
    }

    public String display() { return String.format(Locale.ROOT, "%.3g / %.3g / %.3g", red, green, blue); }

    public int preview() {
        float scale = 1.0f / Math.max(1, peak());
        return 0xFF000000 | srgb(red * scale) << 16 | srgb(green * scale) << 8 | srgb(blue * scale);
    }

    private static int srgb(float channel) {
        double value = channel <= 0.0031308f ? channel * 12.92 : 1.055 * Math.pow(channel, 1.0 / 2.4) - 0.055;
        return (int) Math.round(Math.clamp(value, 0, 1) * 255);
    }
}
