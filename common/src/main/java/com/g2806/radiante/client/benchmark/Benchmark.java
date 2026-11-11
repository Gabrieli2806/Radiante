package com.g2806.radiante.client.benchmark;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.pipeline.Pipeline;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Screenshot;
import net.minecraft.commands.CommandSourceStack;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.entity.Marker;
import net.minecraft.world.level.entity.EntityTypeTest;
import net.minecraft.world.level.levelgen.Heightmap;
import net.minecraft.world.level.storage.LevelResource;
import net.minecraft.world.phys.Vec3;

/**
 * {@code /radiante benchmark}: builds the benchmark scene ({@link BenchmarkScene}) in a test world, flies the camera
 * through it along a fixed sequence of shots ({@link BenchmarkPath}) and reports how fast the frames were drawn: the
 * frame rate of the whole run and of each shot, the slowest hundredth of the frames, and the GPU time of the
 * renderer's stages. One screenshot is kept from each shot, so two runs can be compared by eye as well.
 *
 * <p>It only runs in a singleplayer world whose name starts with {@code Radiante}: the scene replaces 160 blocks of
 * whatever stands at the world's origin. The player's settings are the ones measured; only the field of view, the
 * render distance and the frame limit are fixed for the run and put back after it.
 */
public final class Benchmark {

    private static final String WORLD_PREFIX = "Radiante";
    private static final String MARKER_TAG = "radiante_benchmark_scene_v" + BenchmarkScene.VERSION;
    private static final String MARKER_PREFIX = "radiante_benchmark_scene_v";
    private static final int FOV = 70;
    private static final int RENDER_DISTANCE = 12;
    /** Seconds the scene is left to load and the renderer to build it before the camera starts. */
    private static final double SETTLE_BUILT = 25;
    private static final double SETTLE_LOADED = 10;

    private enum Phase {
        IDLE, BUILDING, SETTLING, RUNNING
    }

    private static volatile Phase phase = Phase.IDLE;
    private static volatile BlockPos origin;
    private static volatile boolean built;
    private static boolean quitWhenDone;
    private static long phaseStart;
    private static long lastFrame;
    private static int shotIndex;
    private static boolean nextTimeSent;
    /** The shot whose picture is taken once the frame being drawn is done, and whether this shot has had its own. */
    private static String pendingShot;
    private static boolean shotPictured;
    private static final List<double[]> shotFrames = new ArrayList<>();
    private static double[] frames = new double[4096];
    private static int frameCount;
    private static final Map<String, double[]> gpuSums = new LinkedHashMap<>();
    private static int gpuSamples;
    private static long lastGpuSample;

    // What the run changes and puts back.
    private static int savedFov;
    private static int savedRenderDistance;
    private static int savedFrameLimit;
    private static boolean savedVsync;
    private static boolean savedHudHidden;
    private static net.minecraft.client.InactivityFpsLimit savedInactivity;

    private Benchmark() {
    }

    public static boolean running() {
        return phase != Phase.IDLE;
    }

    /** Starts the benchmark, or says why it cannot. {@code quit} closes the game when it ends, for scripted runs. */
    public static void start(Minecraft minecraft, boolean quit) {
        if (phase != Phase.IDLE) {
            return;
        }
        MinecraftServer server = minecraft.getSingleplayerServer();
        if (server == null || minecraft.player == null) {
            say(minecraft, Component.translatable("message.radiante.showcase.singleplayer"));
            return;
        }
        String folder = server.getWorldPath(LevelResource.ROOT).normalize().getFileName().toString();
        if (!server.getWorldData().getLevelName().startsWith(WORLD_PREFIX) && !folder.startsWith(WORLD_PREFIX)) {
            say(minecraft, Component.translatable("message.radiante.benchmark.world"));
            return;
        }
        quitWhenDone = quit;
        savedFov = minecraft.options.fov().get();
        savedRenderDistance = minecraft.options.renderDistance().get();
        savedFrameLimit = minecraft.options.framerateLimit().get();
        savedVsync = minecraft.options.enableVsync().get();
        savedHudHidden = minecraft.gui.hud.isHidden();
        savedInactivity = minecraft.options.inactivityFpsLimit().get();
        say(minecraft, Component.translatable("message.radiante.benchmark.building"));
        phase = Phase.BUILDING;
        server.execute(() -> prepare(server));
    }

    /** Stops a run where it is and puts the player's settings back; nothing is reported. */
    public static void stop(Minecraft minecraft) {
        if (phase == Phase.IDLE) {
            return;
        }
        finish(minecraft, false);
        say(minecraft, Component.translatable("message.radiante.benchmark.stopped"));
    }

    // ---- The scene (server thread) ------------------------------------------------------------------------------

    private static void prepare(MinecraftServer server) {
        try {
            ServerLevel level = server.overworld();
            BlockPos found = null;
            boolean current = false;
            for (Marker marker : level.getEntities(EntityTypeTest.forClass(Marker.class),
                entity -> entity.entityTags().stream().anyMatch(tag -> tag.startsWith(MARKER_PREFIX)))) {
                found = marker.blockPosition();
                current = marker.entityTags().contains(MARKER_TAG);
            }
            if (found == null) {
                // The ground the scene stands on, read well outside it.
                int ground = level.getHeight(Heightmap.Types.WORLD_SURFACE, BenchmarkScene.RADIUS + 120, 0);
                found = new BlockPos(0, Math.max(ground, level.getMinY() + 1), 0);
            }
            CommandSourceStack source = server.createCommandSourceStack().withLevel(level)
                .withPosition(Vec3.atLowerCornerOf(found));
            if (System.getenv("RADIANTE_DEV_SCRIPT") == null) {
                source = source.withSuppressedOutput();
            }
            List<String> commands = new ArrayList<>(List.of("gamerule advance_time false",
                "gamerule advance_weather false", "gamerule spawn_mobs false", "gamerule random_tick_speed 0",
                "weather clear", "time set " + BenchmarkPath.SHOTS.get(0).time(), "gamemode spectator @a",
                String.format(Locale.ROOT, "forceload add %d %d %d %d", -BenchmarkScene.RADIUS, -BenchmarkScene.RADIUS,
                    BenchmarkScene.RADIUS, BenchmarkScene.RADIUS)));
            boolean build = !current;
            if (build) {
                BenchmarkScene scene = new BenchmarkScene();
                long start = System.nanoTime();
                scene.place(level, found);
                RadianteClient.LOGGER.info("[benchmark] scene placed in {} ms", (System.nanoTime() - start) / 1_000_000);
                commands.add("kill @e[tag=" + BenchmarkScene.ENTITY_TAG + "]");
                commands.add("kill @e[type=marker,tag=!" + MARKER_TAG + ",distance=..4]");
                commands.addAll(scene.commands());
                commands.add("summon marker ~ ~ ~ {Tags:[\"" + MARKER_TAG + "\"]}");
            }
            for (String command : commands) {
                server.getCommands().performPrefixedCommand(source, command);
            }
            origin = found;
            built = build;
            phaseStart = System.nanoTime();
            phase = Phase.SETTLING;
        } catch (RuntimeException e) {
            RadianteClient.LOGGER.error("[benchmark] could not build the scene", e);
            phase = Phase.IDLE;
        }
    }

    // ---- The run (render thread) --------------------------------------------------------------------------------

    /** Once a frame, before the level is drawn: places the camera and counts the frame. */
    public static void frame() {
        if (phase == Phase.IDLE || phase == Phase.BUILDING) {
            return;
        }
        Minecraft minecraft = Minecraft.getInstance();
        if (minecraft.player == null || minecraft.getSingleplayerServer() == null) {
            finish(minecraft, false);
            return;
        }
        long now = System.nanoTime();
        if (phase == Phase.SETTLING) {
            // Held on the first shot's first frame while the scene loads and its geometry is built.
            apply(minecraft, BenchmarkPath.SHOTS.get(0), 0);
            if (!minecraft.gui.hud.isHidden()) {
                minecraft.gui.hud.toggle();
            }
            minecraft.options.fov().set(FOV);
            minecraft.options.renderDistance().set(RENDER_DISTANCE);
            if ((now - phaseStart) / 1e9 < (built ? SETTLE_BUILT : SETTLE_LOADED)) {
                return;
            }
            minecraft.options.enableVsync().set(false);
            minecraft.options.framerateLimit().set(260);
            // Nobody touches the mouse during a run: left as it is, the game would drop to 30 frames a second after a
            // minute without input.
            minecraft.options.inactivityFpsLimit().set(net.minecraft.client.InactivityFpsLimit.MINIMIZED);
            phase = Phase.RUNNING;
            phaseStart = now;
            lastFrame = now;
            shotIndex = 0;
            nextTimeSent = false;
            frameCount = 0;
            shotFrames.clear();
            gpuSums.clear();
            gpuSamples = 0;
            lastGpuSample = now;
            RadianteClient.LOGGER.info("[benchmark] started: {} shots, {} s", BenchmarkPath.SHOTS.size(),
                BenchmarkPath.totalSeconds());
            return;
        }

        double elapsed = (now - phaseStart) / 1e9;
        double shotStart = 0;
        for (int i = 0; i < shotIndex; i++) {
            shotStart += BenchmarkPath.SHOTS.get(i).seconds();
        }
        BenchmarkPath.Shot shot = BenchmarkPath.SHOTS.get(shotIndex);
        if (frameCount == frames.length) {
            frames = Arrays.copyOf(frames, frames.length * 2);
        }
        frames[frameCount++] = (now - lastFrame) / 1e6;
        lastFrame = now;
        if (now - lastGpuSample > 500_000_000L) {
            lastGpuSample = now;
            sampleGpu();
        }

        double into = elapsed - shotStart;
        // The next shot's time of day is asked for a little early: the server sets it on its next tick, and a cut
        // that changed the light a moment after the picture would show as a flash.
        if (!nextTimeSent && into > shot.seconds() - 0.1 && shotIndex + 1 < BenchmarkPath.SHOTS.size()) {
            nextTimeSent = true;
            setTime(minecraft, BenchmarkPath.SHOTS.get(shotIndex + 1).time());
        }
        if (!shotPictured && into > shot.seconds() * 0.55) {
            shotPictured = true;
            pendingShot = shot.name();
        }
        if (into >= shot.seconds()) {
            closeShot();
            shotIndex++;
            nextTimeSent = false;
            if (shotIndex >= BenchmarkPath.SHOTS.size()) {
                finish(minecraft, true);
                return;
            }
            shot = BenchmarkPath.SHOTS.get(shotIndex);
            into = 0;
        }
        apply(minecraft, shot, into);
    }

    /** After the frame is drawn: keeps one picture of each shot. */
    public static void frameDrawn() {
        if (phase != Phase.RUNNING || pendingShot == null) {
            return;
        }
        Minecraft minecraft = Minecraft.getInstance();
        String name = "benchmark-" + pendingShot + ".png";
        pendingShot = null;
        Screenshot.grab(minecraft.gameDirectory, name, minecraft.gameRenderer.mainRenderTarget(), 1,
            message -> RadianteClient.LOGGER.info("[benchmark] screenshot {}", name));
    }

    private static int shotFirstFrame;

    private static void closeShot() {
        shotFrames.add(Arrays.copyOfRange(frames, shotFirstFrame, frameCount));
        shotFirstFrame = frameCount;
        pendingShot = null;
        shotPictured = false;
    }

    private static void apply(Minecraft minecraft, BenchmarkPath.Shot shot, double seconds) {
        BlockPos base = origin;
        var player = minecraft.player;
        if (base == null || player == null) {
            return;
        }
        BenchmarkPath.Pose pose = BenchmarkPath.pose(shot, seconds);
        double x = base.getX() + 0.5 + pose.x();
        double y = base.getY() + pose.y() - player.getEyeHeight();
        double z = base.getZ() + 0.5 + pose.z();
        player.setYRot(pose.yaw());
        player.setXRot(pose.pitch());
        player.yRotO = pose.yaw();
        player.xRotO = pose.pitch();
        player.setPos(x, y, z);
        player.xo = x;
        player.yo = y;
        player.zo = z;
        player.xOld = x;
        player.yOld = y;
        player.zOld = z;
        player.setDeltaMovement(Vec3.ZERO);
    }

    private static void setTime(Minecraft minecraft, int time) {
        MinecraftServer server = minecraft.getSingleplayerServer();
        if (server != null) {
            server.execute(() -> server.getCommands().performPrefixedCommand(
                server.createCommandSourceStack().withSuppressedOutput(), "time set " + time));
        }
    }

    /** "9.12 ms (world 5.10, DLSS 1.20, ...)": the total and each stage, summed for the run's average. */
    private static void sampleGpu() {
        String stats = Pipeline.gpuStats();
        if (stats == null || stats.isEmpty()) {
            return;
        }
        try {
            int open = stats.indexOf('(');
            String total = (open < 0 ? stats : stats.substring(0, open)).replace("ms", "").trim();
            add("total", Double.parseDouble(total));
            if (open >= 0) {
                for (String part : stats.substring(open + 1, stats.lastIndexOf(')')).split(",")) {
                    String trimmed = part.trim();
                    int space = trimmed.lastIndexOf(' ');
                    if (space > 0) {
                        add(trimmed.substring(0, space), Double.parseDouble(trimmed.substring(space + 1)));
                    }
                }
            }
            gpuSamples++;
        } catch (RuntimeException e) {
            // A line this does not know how to read: the frame times still stand.
        }
    }

    private static void add(String stage, double milliseconds) {
        gpuSums.computeIfAbsent(stage, key -> new double[1])[0] += milliseconds;
    }

    // ---- The end ------------------------------------------------------------------------------------------------

    private static void finish(Minecraft minecraft, boolean completed) {
        boolean wasRunning = phase == Phase.RUNNING;
        phase = Phase.IDLE;
        pendingShot = null;
        shotPictured = false;
        shotFirstFrame = 0;
        if (wasRunning || completed) {
            minecraft.options.enableVsync().set(savedVsync);
            minecraft.options.framerateLimit().set(savedFrameLimit);
            minecraft.options.inactivityFpsLimit().set(savedInactivity);
        }
        minecraft.options.fov().set(savedFov);
        minecraft.options.renderDistance().set(savedRenderDistance);
        if (minecraft.gui.hud.isHidden() != savedHudHidden) {
            minecraft.gui.hud.toggle();
        }
        MinecraftServer server = minecraft.getSingleplayerServer();
        BlockPos base = origin;
        if (server != null && base != null) {
            // Back on the ground, on the dock, looking at the house.
            server.execute(() -> {
                CommandSourceStack source = server.createCommandSourceStack().withSuppressedOutput()
                    .withPosition(Vec3.atLowerCornerOf(base));
                server.getCommands().performPrefixedCommand(source, "gamemode creative @a");
                server.getCommands().performPrefixedCommand(source, "time set 1800");
                server.getCommands().performPrefixedCommand(source,
                    "tp @a ~2.5 ~" + (BenchmarkScene.GROUND + 1.6) + " ~13.5 90 5");
            });
        }
        if (!completed) {
            return;
        }
        double[] all = Arrays.copyOf(frames, frameCount);
        double fps = fps(all);
        double low = low(all);
        double gpu = gpuSamples > 0 && gpuSums.containsKey("total") ? gpuSums.get("total")[0] / gpuSamples : 0;
        Path file = report(minecraft, all);
        RadianteClient.LOGGER.info("[benchmark] done: {} fps average, {} fps 1% low, {} ms GPU, {} frames{}",
            String.format(Locale.ROOT, "%.1f", fps), String.format(Locale.ROOT, "%.1f", low),
            String.format(Locale.ROOT, "%.2f", gpu), frameCount, file == null ? "" : ", " + file);
        say(minecraft, Component.translatable("message.radiante.benchmark.done", String.format(Locale.ROOT, "%.1f", fps),
            String.format(Locale.ROOT, "%.1f", low), String.format(Locale.ROOT, "%.2f", gpu),
            file == null ? "-" : file.getFileName().toString()));
        if (quitWhenDone) {
            minecraft.stop();
        }
    }

    private static double fps(double[] milliseconds) {
        double sum = 0;
        for (double value : milliseconds) {
            sum += value;
        }
        return sum <= 0 ? 0 : milliseconds.length * 1000.0 / sum;
    }

    /** The frame rate of the slowest hundredth of the frames. */
    private static double low(double[] milliseconds) {
        if (milliseconds.length == 0) {
            return 0;
        }
        double[] sorted = milliseconds.clone();
        Arrays.sort(sorted);
        int count = Math.max(1, sorted.length / 100);
        double sum = 0;
        for (int i = sorted.length - count; i < sorted.length; i++) {
            sum += sorted[i];
        }
        return sum <= 0 ? 0 : count * 1000.0 / sum;
    }

    /** Writes the run to radiante/benchmarks/ as JSON; null when it could not be written. */
    private static Path report(Minecraft minecraft, double[] all) {
        StringBuilder json = new StringBuilder("{\n");
        String stamp = LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyyMMdd-HHmmss"));
        json.append("  \"benchmark\": \"radiante\",\n  \"scene\": ").append(BenchmarkScene.VERSION);
        json.append(",\n  \"time\": \"").append(stamp).append("\",\n");
        json.append("  \"version\": \"").append(escape(com.g2806.radiante.platform.RadiantePlatform.INSTANCE.modVersion()));
        json.append("\",\n  \"window\": \"").append(minecraft.getWindow().getWidth()).append('x')
            .append(minecraft.getWindow().getHeight()).append("\",\n");
        json.append("  \"upscaler\": \"").append(escape(Pipeline.describeUpscaler())).append("\",\n");
        json.append(String.format(Locale.ROOT, "  \"seconds\": %.1f,\n  \"frames\": %d,\n  \"fps\": %.2f,\n"
            + "  \"fps_1_percent_low\": %.2f,\n", BenchmarkPath.totalSeconds(), all.length, fps(all), low(all)));
        json.append("  \"shots\": [\n");
        for (int i = 0; i < shotFrames.size(); i++) {
            double[] shot = shotFrames.get(i);
            json.append(String.format(Locale.ROOT, "    {\"name\": \"%s\", \"frames\": %d, \"fps\": %.2f, "
                + "\"fps_1_percent_low\": %.2f}%s\n", BenchmarkPath.SHOTS.get(i).name(), shot.length, fps(shot), low(shot),
                i + 1 < shotFrames.size() ? "," : ""));
        }
        json.append("  ],\n  \"gpu_ms\": {");
        int written = 0;
        for (Map.Entry<String, double[]> stage : gpuSums.entrySet()) {
            json.append(written++ == 0 ? "" : ", ").append('"').append(escape(stage.getKey())).append("\": ")
                .append(String.format(Locale.ROOT, "%.3f", stage.getValue()[0] / Math.max(gpuSamples, 1)));
        }
        json.append("},\n  \"settings\": \"").append(escape(com.g2806.radiante.client.gui.DebugReport.build(minecraft)))
            .append("\"\n}\n");
        try {
            Path folder = RadianteClient.radianceDir.resolve("benchmarks");
            Files.createDirectories(folder);
            Path file = folder.resolve("benchmark-" + stamp + ".json");
            Files.writeString(file, json.toString(), StandardCharsets.UTF_8);
            return file;
        } catch (IOException | RuntimeException e) {
            RadianteClient.LOGGER.warn("[benchmark] could not write the report", e);
            return null;
        }
    }

    private static String escape(String text) {
        if (text == null) {
            return "";
        }
        StringBuilder out = new StringBuilder();
        for (char c : text.toCharArray()) {
            switch (c) {
                case '"' -> out.append("\\\"");
                case '\\' -> out.append("\\\\");
                case '\n' -> out.append("\\n");
                case '\r' -> { }
                case '\t' -> out.append("\\t");
                default -> out.append(c < 0x20 ? ' ' : c);
            }
        }
        return out.toString();
    }

    private static void say(Minecraft minecraft, Component message) {
        if (minecraft.player != null) {
            minecraft.player.sendSystemMessage(message);
        }
    }
}
