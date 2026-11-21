package com.g2806.radiante.client.photo;

import com.g2806.radiante.client.RadianteClient;
import com.mojang.blaze3d.platform.NativeImage;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;
import net.minecraft.client.Minecraft;
import net.minecraft.resources.Identifier;
import net.minecraft.util.Util;

/**
 * Radiante's logo and name in a corner of the photo, for whoever wants to sign what they share (Photo Mode's Logo):
 * the lantern of the settings screen on nothing, no box behind it, and the name in the game's own font, bold, as the
 * settings' side panel writes it.
 * Off unless chosen. The screen shows it where it will be, and the saved PNG gets it once the renderer has written
 * the file: the picture is the renderer's own accumulation, which knows nothing of the game's textures, so the logo is
 * drawn over the file afterwards. The OpenEXR keeps the bare light: it is the one meant for further work.
 */
public final class PhotoWatermark {

    /** Where the logo goes, or nowhere. */
    public enum Corner {
        OFF, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT;

        public String key() {
            return "gui.radiante.photo.logo." + name().toLowerCase(Locale.ROOT);
        }
    }

    public static final Identifier TEXTURE = Identifier.fromNamespaceAndPath("radiante", "textures/gui/photo_watermark.png");
    /** The logo's own size, for the screen's preview. */
    public static final int TEXTURE_WIDTH = 422;
    public static final int TEXTURE_HEIGHT = 114;
    /** The logo's height and its distance from the edges, as shares of the photo's height. */
    public static final float HEIGHT_SHARE = 0.055f;
    public static final float MARGIN_SHARE = 0.025f;

    private static Corner corner = Corner.OFF;

    private PhotoWatermark() {
    }

    public static Corner corner() {
        return corner;
    }

    public static void cycle(int step) {
        corner = Corner.values()[Math.floorMod(corner.ordinal() + step, Corner.values().length)];
    }

    /**
     * Where the logo sits in a picture of the given size: x, y, width and height. The same rule places it on the
     * screen and in the file.
     */
    public static int[] place(Corner where, int width, int height) {
        int h = Math.max(8, Math.round(height * HEIGHT_SHARE));
        int w = Math.round(h * (float) TEXTURE_WIDTH / TEXTURE_HEIGHT);
        int margin = Math.round(height * MARGIN_SHARE);
        boolean right = where == Corner.TOP_RIGHT || where == Corner.BOTTOM_RIGHT;
        boolean bottom = where == Corner.BOTTOM_LEFT || where == Corner.BOTTOM_RIGHT;
        return new int[] {right ? width - margin - w : margin, bottom ? height - margin - h : margin, w, h};
    }

    /** Draws the logo over a saved PNG, off the render thread: reading and writing a 4K file takes a moment. */
    static void applyLater(String pngPath) {
        Corner where = corner;
        if (where == Corner.OFF || pngPath == null || pngPath.isEmpty()) {
            return;
        }
        Minecraft minecraft = Minecraft.getInstance();
        Util.ioPool().execute(() -> {
            Path file = Path.of(pngPath);
            try (InputStream logoStream = minecraft.getResourceManager().open(TEXTURE);
                 NativeImage logo = NativeImage.read(logoStream);
                 InputStream photoStream = Files.newInputStream(file);
                 NativeImage photo = NativeImage.read(photoStream)) {
                draw(photo, logo, where);
                photo.writeToFile(file);
            } catch (IOException | RuntimeException e) {
                RadianteClient.LOGGER.warn("Could not draw the logo over {}", pngPath, e);
            }
        });
    }

    /** The logo scaled to its place, each of its texels blended over the photo by its alpha. */
    private static void draw(NativeImage photo, NativeImage logo, Corner where) {
        int[] rect = place(where, photo.getWidth(), photo.getHeight());
        for (int y = 0; y < rect[3]; y++) {
            int py = rect[1] + y;
            if (py < 0 || py >= photo.getHeight()) {
                continue;
            }
            // Nearest texel: the logo is pixel art, and stays pixels at any size.
            int ly = Math.min(logo.getHeight() - 1, y * logo.getHeight() / rect[3]);
            for (int x = 0; x < rect[2]; x++) {
                int px = rect[0] + x;
                if (px < 0 || px >= photo.getWidth()) {
                    continue;
                }
                int lx = Math.min(logo.getWidth() - 1, x * logo.getWidth() / rect[2]);
                int over = logo.getPixel(lx, ly);
                int alpha = over >>> 24;
                if (alpha == 0) {
                    continue;
                }
                int under = photo.getPixel(px, py);
                int r = blend(under >> 16 & 0xFF, over >> 16 & 0xFF, alpha);
                int g = blend(under >> 8 & 0xFF, over >> 8 & 0xFF, alpha);
                int b = blend(under & 0xFF, over & 0xFF, alpha);
                photo.setPixel(px, py, 0xFF000000 | r << 16 | g << 8 | b);
            }
        }
    }

    private static int blend(int under, int over, int alpha) {
        return (under * (255 - alpha) + over * alpha + 127) / 255;
    }
}
