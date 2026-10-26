package com.g2806.radiante.client;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.phys.Vec3;

/**
 * Development only: records a camera path by flying to places and marking them ({@code /campath} commands, see the
 * Fabric module), instead of typing coordinates into a path file. A path is a start, optional marks in between and an
 * end; the keyframe times are spread over the length of the path by distance, so the speed is even. The result is a
 * {@link DevCamera} path file plus a small JSON file the trailer scripts read ({@code campaths/name.json}) with the
 * dimension and the keyframes.
 */
public final class DevCameraMarks {

    private record Mark(double x, double y, double z, double yaw, double pitch) {
    }

    private static final List<Mark> MARKS = new ArrayList<>();
    /** The time of day the shot is taken at (ticks), or -1 to leave the world's time alone. */
    private static long time = -1L;

    private DevCameraMarks() {
    }

    private static Mark here(Minecraft minecraft) {
        LocalPlayer player = minecraft.player;
        Vec3 eye = player.getEyePosition();
        return new Mark(eye.x, eye.y, eye.z, player.getYRot(), player.getXRot());
    }

    private static String describe(Mark mark) {
        return String.format(Locale.ROOT, "%.1f %.1f %.1f  yaw %.0f pitch %.0f", mark.x, mark.y, mark.z, mark.yaw,
            mark.pitch);
    }

    /** Where the camera is now, for noting coordinates. */
    public static String where(Minecraft minecraft) {
        return describe(here(minecraft));
    }

    /** The time of day the next saved path is shot at: /time set that value before recording it. */
    public static String time(long ticks) {
        time = ticks;
        return "shot time of day: " + ticks;
    }

    public static String start(Minecraft minecraft) {
        MARKS.clear();
        MARKS.add(here(minecraft));
        return "start: " + describe(MARKS.get(0));
    }

    public static String mark(Minecraft minecraft) {
        if (MARKS.isEmpty()) {
            return "no start yet: /campath start first";
        }
        MARKS.add(here(minecraft));
        return "mark " + (MARKS.size() - 1) + ": " + describe(MARKS.get(MARKS.size() - 1));
    }

    /** Adds the end mark and writes the path; the seconds are how long the camera takes from start to end. */
    public static String end(Minecraft minecraft, String name, double seconds) {
        if (MARKS.isEmpty()) {
            return "no start yet: /campath start first";
        }
        MARKS.add(here(minecraft));
        double[] cumulative = new double[MARKS.size()];
        for (int i = 1; i < MARKS.size(); i++) {
            Mark a = MARKS.get(i - 1);
            Mark b = MARKS.get(i);
            cumulative[i] = cumulative[i - 1] + Math.max(0.5, Math.sqrt((a.x - b.x) * (a.x - b.x)
                + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)));
        }
        String dimension = minecraft.level.dimension().identifier().getPath();
        StringBuilder text = new StringBuilder();
        StringBuilder json = new StringBuilder();
        text.append("# ").append(name).append(": recorded in game, dimension ").append(dimension).append('\n');
        text.append("ease 2.5\nhold 0.5\n");
        json.append("{\"dim\": \"").append(dimension).append("\", \"time\": ").append(time < 0 ? "null" : String.valueOf(time))
            .append(", \"ease\": 2.5, \"hold\": 0.5, \"keys\": [");
        for (int i = 0; i < MARKS.size(); i++) {
            Mark mark = MARKS.get(i);
            double t = seconds * cumulative[i] / cumulative[MARKS.size() - 1];
            text.append(String.format(Locale.ROOT, "%.2f %.2f %.2f %.2f %.1f %.1f%n", t, mark.x, mark.y, mark.z,
                mark.yaw, mark.pitch));
            json.append(i == 0 ? "" : ", ").append(String.format(Locale.ROOT, "[%.2f, %.2f, %.2f, %.2f, %.1f, %.1f]",
                t, mark.x, mark.y, mark.z, mark.yaw, mark.pitch));
        }
        json.append("]}\n");
        Path folder = minecraft.gameDirectory.toPath().resolve("campaths");
        try {
            Files.createDirectories(folder);
            Files.writeString(folder.resolve(name + ".txt"), text.toString());
            Files.writeString(folder.resolve(name + ".json"), json.toString());
        } catch (IOException e) {
            RadianteClient.LOGGER.warn("[dev] camera path {} not written", name, e);
            return "not saved: " + e.getMessage();
        }
        String summary = "saved " + name + " (" + MARKS.size() + " points, " + seconds + " s, " + dimension
            + (time < 0 ? ", time not set" : ", time " + time) + "): "
            + folder.resolve(name + ".txt");
        RadianteClient.LOGGER.info("[dev] {}", summary);
        MARKS.clear();
        return summary;
    }

    public static String play(Minecraft minecraft, String name) {
        if (!DevCamera.load(minecraft, name)) {
            return "path " + name + " not found or invalid";
        }
        DevCamera.pose(minecraft);
        DevCamera.play(minecraft, false);
        return "playing " + name;
    }

    public static String stop() {
        DevCamera.stop();
        return "stopped";
    }

    public static List<String> saved(Minecraft minecraft) {
        List<String> names = new ArrayList<>();
        try (var stream = Files.list(minecraft.gameDirectory.toPath().resolve("campaths"))) {
            stream.map(path -> path.getFileName().toString()).filter(file -> file.endsWith(".txt"))
                .filter(file -> !file.endsWith("_a.txt") && !file.endsWith("_b.txt"))
                .map(file -> file.substring(0, file.length() - 4)).sorted().forEach(names::add);
        } catch (IOException e) {
            // No folder yet: nothing saved.
        }
        return names;
    }
}
