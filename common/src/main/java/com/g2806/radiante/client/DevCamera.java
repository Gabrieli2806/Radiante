package com.g2806.radiante.client;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import net.minecraft.client.Minecraft;
import net.minecraft.util.Mth;

/**
 * Development only: smooth camera paths for capturing footage, driven by {@link DevAutomation} steps and inert
 * without them. A path is a text file of keyframes, one per line:
 *
 * <pre>
 * # seconds  x  y  z  yaw  pitch [fov]
 * 0    100.5 80 200.5  -45 10
 * 6    120.5 82 210.5  -20  8
 * # or aimed at a point instead of yaw and pitch:
 * 12   130.5 85 230.5  look 140 70 240
 * </pre>
 *
 * <p>Directives on their own lines: {@code ease <seconds>} (how long the camera takes to speed up at the start and
 * slow down at the end; the speed in between is constant, default a quarter of the path, at most 2 s), {@code fps
 * <n>} (the frame rate of a recording, default 60) and {@code hold <seconds>} (the camera stays at the last keyframe
 * that long after the path ends, still recording, default 0).
 *
 * <p>Positions and angles go through a Catmull-Rom spline with the keyframes' own times as knots, so keyframes may be
 * spaced unevenly without the speed jumping. The camera moves once a frame, not once a tick: by real time while only
 * playing, and by exactly 1/fps per frame while recording, so a recording is smooth at its frame rate however fast
 * the game draws (the world itself then runs slower or faster than the camera when the game does not hold that
 * rate). The player is put in spectator mode first by the script; the camera is the player.
 */
public final class DevCamera {

    private record Key(double t, double x, double y, double z, double yaw, double pitch, double fov) {
    }

    private static List<Key> keys = List.of();
    private static double ease;
    private static double hold;
    private static int fps = 60;
    private static boolean playing;
    private static boolean fixedStep;
    private static long startNanos;
    private static long frameIndex;
    private static String name;
    /** The FOV a path asks for, or NaN to leave Minecraft's own (see DevCameraFovMixin). */
    private static double fovOverride = Double.NaN;

    private DevCamera() {
    }

    /** Loads a path; the file is {@code campaths/<name>.txt} in the game folder, or an absolute path. */
    static boolean load(Minecraft minecraft, String pathName) {
        Path file = pathName.contains(":") || pathName.startsWith("/") ? Path.of(pathName)
            : minecraft.gameDirectory.toPath().resolve("campaths").resolve(pathName + ".txt");
        List<Key> loaded = new ArrayList<>();
        double easeSeconds = -1.0;
        double holdSeconds = 0.0;
        int frameRate = 60;
        try {
            for (String raw : Files.readAllLines(file)) {
                String line = raw.strip();
                int comment = line.indexOf('#');
                if (comment >= 0) {
                    line = line.substring(0, comment).strip();
                }
                if (line.isEmpty()) {
                    continue;
                }
                String[] v = line.split("\\s+");
                switch (v[0].toLowerCase(Locale.ROOT)) {
                    case "ease" -> easeSeconds = Double.parseDouble(v[1]);
                    case "hold" -> holdSeconds = Double.parseDouble(v[1]);
                    case "fps" -> frameRate = Integer.parseInt(v[1]);
                    default -> loaded.add(parseKey(v, loaded));
                }
            }
        } catch (IOException | RuntimeException e) {
            RadianteClient.LOGGER.warn("[dev] camera path {} not loaded", file, e);
            return false;
        }
        if (loaded.isEmpty()) {
            RadianteClient.LOGGER.warn("[dev] camera path {} has no keyframes", file);
            return false;
        }
        loaded.sort((a, b) -> Double.compare(a.t(), b.t()));
        unwrapYaw(loaded);
        keys = loaded;
        double duration = duration();
        ease = easeSeconds >= 0.0 ? Math.min(easeSeconds, duration / 2.0) : Math.min(2.0, duration / 4.0);
        hold = holdSeconds;
        fps = frameRate;
        name = pathName;
        RadianteClient.LOGGER.info("[dev] camera path {}: {} keyframes, {} s, ease {} s", pathName, keys.size(),
            duration, ease);
        return true;
    }

    private static Key parseKey(String[] v, List<Key> previous) {
        double t = Double.parseDouble(v[0]);
        double x = Double.parseDouble(v[1]);
        double y = Double.parseDouble(v[2]);
        double z = Double.parseDouble(v[3]);
        double yaw;
        double pitch;
        int next;
        if (v[4].equalsIgnoreCase("look")) {
            // Aimed at a point: the eye is the keyframe's position, as the camera is placed at the eye (see apply).
            double dx = Double.parseDouble(v[5]) - x;
            double dy = Double.parseDouble(v[6]) - y;
            double dz = Double.parseDouble(v[7]) - z;
            yaw = Math.toDegrees(Math.atan2(-dx, dz));
            pitch = Math.toDegrees(-Math.atan2(dy, Math.sqrt(dx * dx + dz * dz)));
            next = 8;
        } else {
            yaw = Double.parseDouble(v[4]);
            pitch = Double.parseDouble(v[5]);
            next = 6;
        }
        double fov = v.length > next ? Double.parseDouble(v[next]) : Double.NaN;
        return new Key(t, x, y, z, yaw, pitch, fov);
    }

    /** Each yaw is moved by whole turns to the one nearest the previous, so the camera turns the short way. */
    private static void unwrapYaw(List<Key> list) {
        for (int i = 1; i < list.size(); i++) {
            Key previous = list.get(i - 1);
            Key key = list.get(i);
            double yaw = previous.yaw() + Mth.wrapDegrees(key.yaw() - previous.yaw());
            list.set(i, new Key(key.t(), key.x(), key.y(), key.z(), yaw, key.pitch(), key.fov()));
        }
    }

    private static double duration() {
        return keys.get(keys.size() - 1).t() - keys.get(0).t();
    }

    /** Puts the camera at the first keyframe and keeps it there, so the denoisers can settle before a take. */
    static void pose(Minecraft minecraft) {
        playing = false;
        if (!keys.isEmpty()) {
            apply(minecraft, sample(keys.get(0).t()));
            held = keys.get(0);
        }
    }

    private static Key held;

    /** Starts the loaded path; with {@code record} every frame of it goes to a video (see DevRecorder). */
    static void play(Minecraft minecraft, boolean record) {
        if (keys.isEmpty()) {
            return;
        }
        playing = true;
        fixedStep = record;
        frameIndex = 0;
        startNanos = System.nanoTime();
        held = null;
        if (record) {
            DevRecorder.start(minecraft, name, fps);
        }
        RadianteClient.LOGGER.info("[dev] camera path {} playing{}", name, record ? ", recording" : "");
    }

    static void stop() {
        playing = false;
        held = null;
        fovOverride = Double.NaN;
    }

    public static double fovOverride() {
        return fovOverride;
    }

    static boolean isPlaying() {
        return playing;
    }

    /** Once a frame before the level is drawn: the camera for this frame. */
    static void frame(Minecraft minecraft) {
        if (minecraft.player == null) {
            return;
        }
        if (!playing) {
            if (held != null) {
                apply(minecraft, held);
            }
            return;
        }
        double elapsed = fixedStep ? frameIndex / (double) fps : (System.nanoTime() - startNanos) / 1.0e9;
        frameIndex++;
        double duration = duration();
        apply(minecraft, sample(keys.get(0).t() + remap(Math.min(elapsed, duration), duration)));
        if (elapsed >= duration + hold) {
            playing = false;
            held = keys.get(keys.size() - 1);
            if (DevRecorder.isRecording()) {
                DevRecorder.stop();
            }
            RadianteClient.LOGGER.info("[dev] camera path {} done after {} frames", name, frameIndex);
        }
    }

    /**
     * Time along the path for time since its start: speeds up over {@code ease} seconds, keeps a constant speed and
     * slows down over the last {@code ease} seconds, ending at the end of the path on time. The speed is a smooth
     * step during the ease, so it has no corner where the motion starts or stops.
     */
    private static double remap(double t, double duration) {
        if (ease <= 0.0) {
            return t;
        }
        // Constant speed v in the middle, ramps that cover half the distance they would at full speed.
        double v = duration / (duration - ease);
        if (t < ease) {
            double u = t / ease;
            // Integral of a smoothstep speed curve from 0 to u, scaled to the ramp's length.
            return v * ease * (u * u * u - 0.5 * u * u * u * u);
        }
        if (t > duration - ease) {
            double u = (duration - t) / ease;
            return duration - v * ease * (u * u * u - 0.5 * u * u * u * u);
        }
        return v * ease * 0.5 + v * (t - ease);
    }

    /** The camera at a time along the path. */
    private static Key sample(double t) {
        int n = keys.size();
        if (n == 1 || t <= keys.get(0).t()) {
            return keys.get(0);
        }
        if (t >= keys.get(n - 1).t()) {
            return keys.get(n - 1);
        }
        int i = 0;
        while (keys.get(i + 1).t() < t) {
            i++;
        }
        Key k0 = keys.get(Math.max(i - 1, 0));
        Key k1 = keys.get(i);
        Key k2 = keys.get(i + 1);
        Key k3 = keys.get(Math.min(i + 2, n - 1));
        double h = k2.t() - k1.t();
        double s = (t - k1.t()) / h;
        double fov = Double.isNaN(k1.fov()) || Double.isNaN(k2.fov()) ? Double.NaN
            : hermite(k0.fov(), k1.fov(), k2.fov(), k3.fov(), k0.t(), k1.t(), k2.t(), k3.t(), s);
        return new Key(t,
            hermite(k0.x(), k1.x(), k2.x(), k3.x(), k0.t(), k1.t(), k2.t(), k3.t(), s),
            hermite(k0.y(), k1.y(), k2.y(), k3.y(), k0.t(), k1.t(), k2.t(), k3.t(), s),
            hermite(k0.z(), k1.z(), k2.z(), k3.z(), k0.t(), k1.t(), k2.t(), k3.t(), s),
            hermite(k0.yaw(), k1.yaw(), k2.yaw(), k3.yaw(), k0.t(), k1.t(), k2.t(), k3.t(), s),
            hermite(k0.pitch(), k1.pitch(), k2.pitch(), k3.pitch(), k0.t(), k1.t(), k2.t(), k3.t(), s),
            fov);
    }

    /**
     * Catmull-Rom between p1 and p2 with tangents from the neighbours over their real time spans, so unevenly spaced
     * keyframes keep a steady speed. At the ends the missing neighbour is the end itself.
     */
    private static double hermite(double p0, double p1, double p2, double p3, double t0, double t1, double t2,
        double t3, double s) {
        double h = t2 - t1;
        double m1 = t2 > t0 ? (p2 - p0) / (t2 - t0) * h : p2 - p1;
        double m2 = t3 > t1 ? (p3 - p1) / (t3 - t1) * h : p2 - p1;
        double s2 = s * s;
        double s3 = s2 * s;
        return (2 * s3 - 3 * s2 + 1) * p1 + (s3 - 2 * s2 + s) * m1 + (-2 * s3 + 3 * s2) * p2 + (s3 - s2) * m2;
    }

    /** The keyframe's position is the eye's; the player is placed below it by the eye height. */
    private static void apply(Minecraft minecraft, Key key) {
        var player = minecraft.player;
        float yaw = (float) key.yaw();
        float pitch = Mth.clamp((float) key.pitch(), -90.0F, 90.0F);
        player.setYRot(yaw);
        player.setXRot(pitch);
        player.yRotO = yaw;
        player.xRotO = pitch;
        player.setYHeadRot(yaw);
        player.yHeadRotO = yaw;
        double x = key.x();
        double y = key.y() - player.getEyeHeight();
        double z = key.z();
        player.setDeltaMovement(net.minecraft.world.phys.Vec3.ZERO);
        player.setPos(x, y, z);
        player.xo = x;
        player.yo = y;
        player.zo = z;
        player.xOld = x;
        player.yOld = y;
        player.zOld = z;
        fovOverride = key.fov();
    }
}
