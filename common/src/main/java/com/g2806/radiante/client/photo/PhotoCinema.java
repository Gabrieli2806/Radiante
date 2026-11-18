package com.g2806.radiante.client.photo;

import com.g2806.radiante.client.proxy.vulkan.PhotoProxy;
import java.util.Locale;
import net.minecraft.client.Minecraft;

/**
 * Photo Mode's framing and scene controls (the Cinematic panel): the frame the photo is cut to, the composition
 * guides over the view, lens presets by focal length, and in singleplayer the time of day and the weather.
 */
public final class PhotoCinema {

    /** A frame: its name as shown and its width over height; 0 keeps the screen's. */
    public record Frame(String label, float aspect) {
    }

    /** Free (the screen), widescreen, the cinema ratios, the old television, square and a phone held upright. */
    public static final Frame[] FRAMES = {
        new Frame("gui.radiante.photo.frame.free", 0.0f),
        new Frame("16:9", 16.0f / 9.0f),
        new Frame("2.39:1", 2.39f),
        new Frame("1.85:1", 1.85f),
        new Frame("4:3", 4.0f / 3.0f),
        new Frame("1:1", 1.0f),
        new Frame("9:16", 9.0f / 16.0f),
    };

    /** The composition guides drawn over the frame (never saved). */
    public enum Guides { OFF, THIRDS, GOLDEN, CENTER, DIAGONALS }

    /** Lens presets of a full-frame camera, by focal length in millimetres. */
    private static final int[] LENSES = {14, 24, 35, 50, 85, 135};
    /** The height of a full-frame sensor, in millimetres: what turns a focal length into the vertical view. */
    private static final float SENSOR_HEIGHT_MM = 24.0f;

    private static int frame;
    private static Guides guides = Guides.OFF;

    private PhotoCinema() {
    }

    public static Frame frame() {
        return FRAMES[frame];
    }

    public static void cycleFrame() {
        frame = (frame + 1) % FRAMES.length;
        push();
    }

    public static Guides guides() {
        return guides;
    }

    public static void cycleGuides() {
        guides = Guides.values()[(guides.ordinal() + 1) % Guides.values().length];
    }

    /** Hands the frame to the native side for the saved photo; called again whenever Photo Mode is entered. */
    static void push() {
        PhotoProxy.setCaptureAspect(FRAMES[frame].aspect());
    }

    /** The focal length a vertical view of {@code fovDegrees} has on a full-frame camera, in millimetres. */
    public static float focalLength(float fovDegrees) {
        return SENSOR_HEIGHT_MM * 0.5f / (float) Math.tan(Math.toRadians(fovDegrees) * 0.5);
    }

    /** The vertical view of a full-frame lens of {@code millimetres}, in degrees. */
    static float fovFor(float millimetres) {
        return (float) Math.toDegrees(2.0 * Math.atan(SENSOR_HEIGHT_MM * 0.5 / millimetres));
    }

    /** The next lens preset longer than the view now, back to the widest after the longest. */
    public static void nextLens() {
        float now = focalLength(PhotoMode.fovDegrees());
        int pick = LENSES[0];
        for (int lens : LENSES) {
            if (lens > now + 0.5f) {
                pick = lens;
                break;
            }
        }
        PhotoMode.setFovDegrees(fovFor(pick));
    }

    public static String focalLabel() {
        return String.format(Locale.ROOT, "%d mm", Math.round(focalLength(PhotoMode.fovDegrees())));
    }

    /** True where the scene controls work: a singleplayer world, whose time and weather the player owns. */
    public static boolean ownsWorld(Minecraft minecraft) {
        return minecraft.getSingleplayerServer() != null && minecraft.level != null;
    }

    /** The time of day, 0 to 24000 (6000 noon); -1 without a world. */
    public static long dayTime(Minecraft minecraft) {
        return minecraft.level == null ? -1L : Math.floorMod(minecraft.level.getOverworldClockTime(), 24000L);
    }

    /** Runs a command as the singleplayer world's own server would, with no output in the chat. */
    private static void serverCommand(Minecraft minecraft, String command) {
        var server = minecraft.getSingleplayerServer();
        if (server == null) {
            return;
        }
        server.execute(() -> server.getCommands().performPrefixedCommand(
            server.createCommandSourceStack().withSuppressedOutput(), command));
    }

    /** Sets the time of day in a singleplayer world; the frozen sky is let go so the picture follows it. */
    public static void setDayTime(Minecraft minecraft, long ticks) {
        if (!ownsWorld(minecraft)) {
            return;
        }
        serverCommand(minecraft, "time set " + Math.floorMod(ticks, 24000L));
        PhotoMode.releaseFrozenSky(1500L);
    }

    /** Clear, rain and thunder in turn, in a singleplayer world. */
    public static void cycleWeather(Minecraft minecraft) {
        if (!ownsWorld(minecraft) || minecraft.level == null) {
            return;
        }
        String next = minecraft.level.isThundering() ? "clear" : minecraft.level.isRaining() ? "thunder" : "rain";
        serverCommand(minecraft, "weather " + next);
        // Rain fades in over a few seconds of ticks.
        PhotoMode.releaseFrozenSky(6000L);
    }

    public static String weatherKey(Minecraft minecraft) {
        if (minecraft.level == null) {
            return "gui.radiante.photo.weather.clear";
        }
        return minecraft.level.isThundering() ? "gui.radiante.photo.weather.thunder"
            : minecraft.level.isRaining() ? "gui.radiante.photo.weather.rain" : "gui.radiante.photo.weather.clear";
    }

    /** The time of day as a clock reads it, 06:00 at sunrise (tick 0). */
    public static String clock(long ticks) {
        long minutes = Math.floorMod((ticks + 6000L) * 60L / 1000L, 24L * 60L);
        return String.format(Locale.ROOT, "%02d:%02d", minutes / 60, minutes % 60);
    }
}
