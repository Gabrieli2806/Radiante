package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.addon.AddonImpl;
import com.g2806.radiante.platform.RadiantePlatform;
import com.mojang.blaze3d.platform.NativeImage;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.texture.DynamicTexture;
import net.minecraft.resources.Identifier;
import net.minecraft.server.packs.resources.Resource;

/**
 * The icon beside each addon's heading: the texture the addon named, or else its mod's own icon as the loader lists
 * it. Each is read once into a texture of its own and kept for the rest of the session; an addon without one, or with
 * one that cannot be read, gets none, logged once.
 */
final class AddonIcons {

    /** An icon's texture and its size in pixels, to draw it at its own proportions. */
    record Icon(Identifier texture, int width, int height) {
    }

    /**
     * Icons larger than this are shrunk on load, averaging the pixels: drawn at 16 by nearest sampling, a 512 pixel
     * logo would come out as scattered dots. Pixel-art icons at 32 or below are kept as they are.
     */
    private static final int LARGEST = 32;

    private static final Map<String, Optional<Icon>> ICONS = new HashMap<>();

    private AddonIcons() {
    }

    /** The addon's icon, or null when it has none. Called on the render thread, from the settings screen. */
    static Icon of(AddonImpl addon) {
        return ICONS.computeIfAbsent(addon.id(), id -> Optional.ofNullable(load(addon))).orElse(null);
    }

    private static Icon load(AddonImpl addon) {
        byte[] png = null;
        Identifier named = addon.iconTexture();
        if (named != null) {
            try {
                Optional<Resource> resource = Minecraft.getInstance().getResourceManager().getResource(named);
                if (resource.isPresent()) {
                    try (InputStream in = resource.get().open()) {
                        png = in.readAllBytes();
                    }
                } else {
                    RadianteClient.LOGGER.warn("Addon '{}': its icon {} was not found; using its mod's icon",
                        addon.id(), named);
                }
            } catch (IOException | RuntimeException e) {
                RadianteClient.LOGGER.warn("Addon '{}': its icon {} could not be read; using its mod's icon",
                    addon.id(), named, e);
            }
        }
        if (png == null) {
            try {
                png = RadiantePlatform.INSTANCE.modIcon(addon.id());
            } catch (IOException | RuntimeException | LinkageError e) {
                RadianteClient.LOGGER.warn("Addon '{}': its mod's icon could not be read; shown without one",
                    addon.id(), e);
                return null;
            }
        }
        if (png == null) {
            return null;
        }
        NativeImage image = null;
        try {
            image = NativeImage.read(png);
            NativeImage shown = shrink(image);
            if (shown != image) {
                image.close();
                image = shown;
            }
            Identifier id = Identifier.fromNamespaceAndPath("radiante", "addon_icon/" + addon.id());
            int width = image.getWidth();
            int height = image.getHeight();
            NativeImage pixels = image;
            // The texture owns the image from here on, and closes it with itself.
            DynamicTexture texture = new DynamicTexture(() -> "Radiante addon icon " + addon.id(), pixels);
            image = null;
            Minecraft.getInstance().getTextureManager().register(id, texture);
            return new Icon(id, width, height);
        } catch (IOException | RuntimeException e) {
            RadianteClient.LOGGER.warn("Addon '{}': its icon is not a PNG that can be read; shown without one",
                addon.id(), e);
            return null;
        } finally {
            if (image != null) {
                image.close();
            }
        }
    }

    /** The image at most {@link #LARGEST} pixels on its long side, each pixel the average of those it covers. */
    static NativeImage shrink(NativeImage image) {
        int width = image.getWidth();
        int height = image.getHeight();
        int longest = Math.max(width, height);
        if (longest <= LARGEST) {
            return image;
        }
        int toWidth = Math.max(1, Math.round(width * (float) LARGEST / longest));
        int toHeight = Math.max(1, Math.round(height * (float) LARGEST / longest));
        NativeImage out = new NativeImage(toWidth, toHeight, false);
        for (int y = 0; y < toHeight; y++) {
            int y0 = y * height / toHeight;
            int y1 = Math.max(y0 + 1, (y + 1) * height / toHeight);
            for (int x = 0; x < toWidth; x++) {
                int x0 = x * width / toWidth;
                int x1 = Math.max(x0 + 1, (x + 1) * width / toWidth);
                // Colour weighted by alpha, so the transparent edge of a round logo does not darken its rim.
                long a = 0;
                long r = 0;
                long g = 0;
                long b = 0;
                for (int sy = y0; sy < y1; sy++) {
                    for (int sx = x0; sx < x1; sx++) {
                        int argb = image.getPixel(sx, sy);
                        int alpha = argb >>> 24;
                        a += alpha;
                        r += (long) ((argb >> 16) & 0xFF) * alpha;
                        g += (long) ((argb >> 8) & 0xFF) * alpha;
                        b += (long) (argb & 0xFF) * alpha;
                    }
                }
                int count = (x1 - x0) * (y1 - y0);
                int argb = 0;
                if (a > 0) {
                    argb = (int) (a / count) << 24 | (int) (r / a) << 16 | (int) (g / a) << 8 | (int) (b / a);
                }
                out.setPixel(x, y, argb);
            }
        }
        return out;
    }
}
