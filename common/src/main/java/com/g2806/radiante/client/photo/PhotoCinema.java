package com.g2806.radiante.client.photo;

import com.g2806.radiante.client.proxy.vulkan.PhotoProxy;
import java.util.Locale;
import net.minecraft.client.Minecraft;

/**
 * Photo Mode's framing and scene controls (the Cinematic panel): the frame the photo is cut to, the composition
 * guides over the view, lens presets by focal length, and the photo's own time of day and weather.
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

    public static void cycleFrame(int step) {
        frame = Math.floorMod(frame + step, FRAMES.length);
        push();
    }

    public static Guides guides() {
        return guides;
    }

    public static void cycleGuides(int step) {
        guides = Guides.values()[Math.floorMod(guides.ordinal() + step, Guides.values().length)];
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

    /**
     * The next lens preset longer than the view now, back to the widest after the longest; with -1 the next
     * shorter one, back to the longest after the widest.
     */
    public static void cycleLens(int step) {
        float now = focalLength(PhotoMode.fovDegrees());
        int pick = step > 0 ? LENSES[0] : LENSES[LENSES.length - 1];
        if (step > 0) {
            for (int lens : LENSES) {
                if (lens > now + 0.5f) {
                    pick = lens;
                    break;
                }
            }
        } else {
            for (int i = LENSES.length - 1; i >= 0; i--) {
                if (LENSES[i] < now - 0.5f) {
                    pick = LENSES[i];
                    break;
                }
            }
        }
        PhotoMode.setFovDegrees(fovFor(pick));
    }

    public static String focalLabel() {
        return String.format(Locale.ROOT, "%d mm", Math.round(focalLength(PhotoMode.fovDegrees())));
    }

    // ---- the scene: a time of day and a weather for the photo alone
    //
    // Seen by this client only, while Photo Mode is open: the clocks and the rain the client reads answer with the
    // photo's (ClientClockInstanceMixin, LevelWeatherMixin), so the sky, the light and the wet ground all follow, in
    // a server's world as in the player's own, and nothing is changed for anyone, the player's world included (it
    // used to be set with /time and /weather, which only a singleplayer world allows and which stayed after the
    // photo). Leaving Photo Mode puts the world's own back.

    public static final int WEATHER_WORLD = -1;
    public static final int WEATHER_CLEAR = 0;
    public static final int WEATHER_RAIN = 1;
    public static final int WEATHER_THUNDER = 2;

    private static volatile long timeOverride = -1L;
    private static volatile int weatherOverride = WEATHER_WORLD;

    /** The photo's time of day in ticks (0 to 23999), or -1 for the world's own. */
    public static long timeOverride() {
        return PhotoMode.isActive() ? timeOverride : -1L;
    }

    /** The photo's rain, 0 or 1, or -1 for the world's own. */
    public static float rainOverride() {
        int weather = PhotoMode.isActive() ? weatherOverride : WEATHER_WORLD;
        return weather == WEATHER_WORLD ? -1.0f : weather == WEATHER_CLEAR ? 0.0f : 1.0f;
    }

    /** The photo's thunder, 0 or 1, or -1 for the world's own. */
    public static float thunderOverride() {
        int weather = PhotoMode.isActive() ? weatherOverride : WEATHER_WORLD;
        return weather == WEATHER_WORLD ? -1.0f : weather == WEATHER_THUNDER ? 1.0f : 0.0f;
    }

    /** Whether the photo has a time or a weather of its own. */
    public static boolean sceneChanged() {
        return timeOverride >= 0L || weatherOverride != WEATHER_WORLD;
    }

    /** True where the scene controls work: any world. */
    public static boolean ownsWorld(Minecraft minecraft) {
        return minecraft.level != null;
    }

    /** The time of day, 0 to 24000 (6000 noon), the photo's when it has one; -1 without a world. */
    public static long dayTime(Minecraft minecraft) {
        return minecraft.level == null ? -1L : Math.floorMod(minecraft.level.getOverworldClockTime(), 24000L);
    }

    /**
     * After the photo's time or weather changed: what the game worked out from the old ones this tick is dropped,
     * and the frozen sky is let go for a moment so the picture follows.
     */
    private static void follow(Minecraft minecraft, long followMillis) {
        if (minecraft.level != null) {
            minecraft.level.environmentAttributes().invalidateTickCache();
        }
        PhotoMode.releaseFrozenSky(followMillis);
    }

    /** Sets the photo's time of day. */
    public static void setDayTime(Minecraft minecraft, long ticks) {
        if (!ownsWorld(minecraft)) {
            return;
        }
        timeOverride = Math.floorMod(ticks, 24000L);
        follow(minecraft, 600L);
    }

    private static int weatherNow(Minecraft minecraft) {
        if (weatherOverride != WEATHER_WORLD) {
            return weatherOverride;
        }
        return minecraft.level == null ? WEATHER_CLEAR : minecraft.level.isThundering() ? WEATHER_THUNDER
            : minecraft.level.isRaining() ? WEATHER_RAIN : WEATHER_CLEAR;
    }

    /** Clear, rain and thunder in turn (or back, with -1), for the photo. */
    public static void cycleWeather(Minecraft minecraft, int step) {
        if (!ownsWorld(minecraft)) {
            return;
        }
        weatherOverride = Math.floorMod(weatherNow(minecraft) + step, 3);
        // The wet ground and the puddles take a moment of the world running to follow.
        follow(minecraft, 2500L);
    }

    /** The world's own time and weather again. */
    public static void resetScene(Minecraft minecraft) {
        boolean changed = sceneChanged();
        clearScene();
        if (changed && minecraft != null) {
            follow(minecraft, 2500L);
        }
    }

    /** Leaving Photo Mode: nothing of the photo's scene stays. */
    static void clearScene() {
        timeOverride = -1L;
        weatherOverride = WEATHER_WORLD;
        scene = -1;
    }

    /** Ready-made scenes: a time of day and a weather, set together. */
    public enum Scene {
        SUNRISE(23300L, WEATHER_CLEAR), MORNING(1500L, WEATHER_CLEAR), NOON(6000L, WEATHER_CLEAR),
        GOLDEN_HOUR(11800L, WEATHER_CLEAR), SUNSET(12600L, WEATHER_CLEAR), BLUE_HOUR(13300L, WEATHER_CLEAR),
        MOONLIGHT(18000L, WEATHER_CLEAR), RAINY_DAY(8000L, WEATHER_RAIN), STORM(10500L, WEATHER_THUNDER),
        STORMY_NIGHT(17000L, WEATHER_THUNDER);

        final long time;
        final int weather;

        Scene(long time, int weather) {
            this.time = time;
            this.weather = weather;
        }

        public String key() {
            return "gui.radiante.photo.scene." + name().toLowerCase(Locale.ROOT);
        }
    }

    private static int scene = -1;

    /** The last scene set, or null before any. */
    public static Scene scene() {
        return scene < 0 ? null : Scene.values()[scene];
    }

    /** Sets the next scene's time and weather (the one before, with -1). */
    public static void cycleScene(Minecraft minecraft, int step) {
        if (!ownsWorld(minecraft)) {
            return;
        }
        int count = Scene.values().length;
        scene = scene < 0 ? (step > 0 ? 0 : count - 1) : Math.floorMod(scene + step, count);
        Scene next = Scene.values()[scene];
        timeOverride = next.time;
        weatherOverride = next.weather;
        follow(minecraft, 2500L);
    }

    /** Ready-made camera looks: the lens, the depth of field and the exposure, set together. */
    public enum Look {
        // fov in degrees, aperture radius in blocks (0 is a pinhole: everything sharp), focus in blocks, exposure EV.
        NATURAL(70.0f, 0.0f, 10.0f, 0.0f), LANDSCAPE(85.0f, 0.0f, 32.0f, -0.3f), PORTRAIT(35.0f, 0.12f, 4.0f, 0.2f),
        MACRO(25.0f, 0.25f, 1.5f, 0.3f), CINEMATIC(45.0f, 0.06f, 8.0f, -0.5f), NIGHT(60.0f, 0.02f, 12.0f, 1.0f),
        DRAMATIC(30.0f, 0.08f, 6.0f, -1.0f);

        final float fov;
        final float aperture;
        final float focus;
        final float exposure;

        Look(float fov, float aperture, float focus, float exposure) {
            this.fov = fov;
            this.aperture = aperture;
            this.focus = focus;
            this.exposure = exposure;
        }

        public String key() {
            return "gui.radiante.photo.look." + name().toLowerCase(Locale.ROOT);
        }
    }

    private static int look = -1;

    /** The last look set, or null before any. */
    public static Look look() {
        return look < 0 ? null : Look.values()[look];
    }

    /** Sets the next look (the one before, with -1). */
    public static void cycleLook(int step) {
        int count = Look.values().length;
        look = look < 0 ? (step > 0 ? 0 : count - 1) : Math.floorMod(look + step, count);
        Look next = Look.values()[look];
        PhotoMode.setFovDegrees(next.fov);
        PhotoMode.setApertureRadius(next.aperture);
        PhotoMode.setFocusDistance(next.focus);
        PhotoMode.setExposureBias(next.exposure);
    }

    public static String weatherKey(Minecraft minecraft) {
        int weather = weatherNow(minecraft);
        return weather == WEATHER_THUNDER ? "gui.radiante.photo.weather.thunder"
            : weather == WEATHER_RAIN ? "gui.radiante.photo.weather.rain" : "gui.radiante.photo.weather.clear";
    }

    /** The time of day as a clock reads it, 06:00 at sunrise (tick 0). */
    public static String clock(long ticks) {
        long minutes = Math.floorMod((ticks + 6000L) * 60L / 1000L, 24L * 60L);
        return String.format(Locale.ROOT, "%02d:%02d", minutes / 60, minutes % 60);
    }
}
