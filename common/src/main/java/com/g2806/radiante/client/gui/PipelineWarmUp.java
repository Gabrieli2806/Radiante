package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.compat.vivecraft.VivecraftCompat;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.platform.RadiantePlatform;
import com.g2806.radiante.client.proxy.vulkan.RendererProxy;
import com.g2806.radiante.client.render.OutputResolution;
import com.g2806.radiante.client.render.RadianteRenderer;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Properties;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Stream;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.network.chat.Component;

/**
 * Compiles the renderer's shaders and ray tracing pipelines in the background after the mod, its shaders or the
 * graphics driver changed, so the first world does not spend many seconds on it. What is compiled is kept on disk (the
 * SPIR-V cache and the driver's pipeline cache), keyed by the shader sources and the driver, so an update that touches
 * them finds the caches cold and the first frame of the first world used to build everything while the game stood
 * still (about 25 s for the two ray tracing pipelines on a fast GPU).
 *
 * <p>The start-up compares a fingerprint of the installed internals (mod version, native library, shader files, GPU
 * and driver) with the one stored in {@code radiante/warmup.properties}. When it differs, or the pipeline cache file
 * is missing, a few seconds after the title screen shows (and while no world is open) the world pipeline is built on
 * a worker thread while the menus keep running, with {@link RebuildToast} showing the progress. The fingerprint is
 * stored only after a build succeeded, so a failure or a closed game tries again next start. A world opened meanwhile
 * waits for the build under the renderer's lock instead of starting a second one.
 *
 * <p>Development: {@code -Dradiante.warmup=force} (or the {@code warmup} step of a dev script) runs it whatever the
 * fingerprint says, {@code -Dradiante.warmup=off} never runs it.
 */
public final class PipelineWarmUp {

    private static final String FILE = "warmup.properties";
    private static final String KEY_FINGERPRINT = "fingerprint";
    private static final String KEY_VERSION = "version";
    private static final String PIPELINE_CACHE = "cache/pipeline_cache.bin";
    /** Ticks on the title screen before the warm-up starts (4 s): the menus settle and the player may already be joining. */
    private static final int DELAY_TICKS = 80;
    /** Shares of the progress line: the driver's pipeline compile takes most of the time. */
    private static final float SHADER_SHARE = 0.3f;

    private enum State {
        START, HASHING, WAITING, RUNNING, IN_WORLD, FINISHED
    }

    private static State state = State.START;
    private static CompletableFuture<String> hashing;
    private static String currentFingerprint;
    private static boolean forced;
    private static int titleTicks;
    private static Thread worker;
    /** The worker's outcome: null while it runs, then "" for success or the reason it failed. */
    private static final AtomicReference<String> outcome = new AtomicReference<>();
    private static long startedAt;

    private PipelineWarmUp() {
    }

    /** Development: starts the warm-up on the next tick whatever the stored fingerprint says. */
    public static void force() {
        forced = true;
        if (state == State.FINISHED || state == State.WAITING) {
            state = State.WAITING;
            titleTicks = DELAY_TICKS;
        }
    }

    /** Every client tick. */
    public static void tick(Minecraft minecraft) {
        try {
            step(minecraft);
        } catch (RuntimeException | LinkageError e) {
            // Never costs the player the game: the first world compiles as it always did.
            RadianteClient.LOGGER.warn("[warmup] stopped: {}", e.toString());
            state = State.FINISHED;
        }
    }

    private static void step(Minecraft minecraft) {
        switch (state) {
            case START -> {
                String mode = System.getProperty("radiante.warmup", "");
                if (mode.equalsIgnoreCase("off")) {
                    state = State.FINISHED;
                    return;
                }
                forced |= mode.equalsIgnoreCase("force");
                if (!RadianteRenderer.isActive()) {
                    // The renderer starts with the device, before the first menu: none by then means none at all.
                    if (minecraft.gui.screen() instanceof TitleScreen) {
                        state = State.FINISHED;
                    }
                    return;
                }
                if (!RadianteRenderer.isRayTracingEnabled() || VivecraftCompat.isVrActive()) {
                    state = State.FINISHED;
                    return;
                }
                String identity = RadianteRenderer.gpuIdentity();
                hashing = CompletableFuture.supplyAsync(() -> fingerprint(identity));
                state = State.HASHING;
            }
            case HASHING -> {
                if (!hashing.isDone()) {
                    return;
                }
                currentFingerprint = hashing.join();
                boolean cold = !Files.isRegularFile(RadianteClient.radianceDir.resolve(PIPELINE_CACHE));
                if (!forced && !cold && currentFingerprint != null && currentFingerprint.equals(storedFingerprint())) {
                    state = State.FINISHED;
                    return;
                }
                RadianteClient.LOGGER.info("[warmup] internals changed (cache {}): compiling shaders after the title "
                    + "screen shows", cold ? "missing" : "stale");
                state = State.WAITING;
                // Said at once, not when the work starts: the player sees why the game is about to be busy.
                RebuildToast.running(minecraft, 0.0f, Component.translatable("toast.radiante.warmup"),
                    Component.translatable("toast.radiante.warmup.soon"));
            }
            case WAITING -> waiting(minecraft);
            case RUNNING -> running(minecraft);
            case IN_WORLD -> {
                // A world opened first built the pipeline with its first frame: the caches are warm now.
                if (minecraft.level == null) {
                    state = State.FINISHED;
                } else if (RadianteRenderer.levelFrames() > 0) {
                    remember();
                    RebuildToast.done(minecraft, Component.translatable("toast.radiante.warmup.done"),
                        Component.translatable("toast.radiante.warmup.done.detail"));
                    state = State.FINISHED;
                }
            }
            case FINISHED -> {
            }
        }
    }

    private static void waiting(Minecraft minecraft) {
        if (!RadianteRenderer.isActive() || !RadianteRenderer.isRayTracingEnabled()) {
            state = State.FINISHED;
            return;
        }
        if (minecraft.level != null) {
            state = State.IN_WORLD;
            return;
        }
        if (!(minecraft.gui.screen() instanceof TitleScreen) && titleTicks < DELAY_TICKS) {
            return;
        }
        if (++titleTicks < DELAY_TICKS) {
            return;
        }
        start(minecraft);
    }

    private static void start(Minecraft minecraft) {
        int width = minecraft.gameRenderer.mainRenderTarget().width;
        int height = minecraft.gameRenderer.mainRenderTarget().height;
        int[] size = OutputResolution.byName(Options.outputResolution).outputSize(width, height,
            Options.outputResolutionPercent);
        outcome.set(null);
        startedAt = System.nanoTime();
        // The renderer compiles shaders recursively, so the thread gets the same large stack its start-up has.
        worker = new Thread(null, () -> {
            String result;
            try {
                result = RendererProxy.warmUpPipeline(size[0], size[1]) ? "" : "the pipeline could not be built";
            } catch (Throwable e) {
                result = e.toString();
            }
            outcome.set(result);
        }, "Radiante warm-up", 512L * 1024L * 1024L);
        worker.setDaemon(true);
        worker.start();
        state = State.RUNNING;
        RebuildToast.running(minecraft, 0.0f, Component.translatable("toast.radiante.warmup"),
            Component.translatable("toast.radiante.starting"));
        RadianteClient.LOGGER.info("[warmup] started at {}x{}", size[0], size[1]);
    }

    private static void running(Minecraft minecraft) {
        String result = outcome.get();
        if (result == null) {
            showProgress(minecraft);
            return;
        }
        long millis = (System.nanoTime() - startedAt) / 1_000_000L;
        if (result.isEmpty()) {
            remember();
            RebuildToast.done(minecraft, Component.translatable("toast.radiante.warmup.done"),
                Component.translatable("toast.radiante.warmup.done.detail"));
            RadianteClient.LOGGER.info("[warmup] done in {} ms", millis);
        } else {
            // The pipeline is half made after a failed build: describing it again has the next frame build it.
            Pipeline.build();
            RebuildToast.failed(minecraft, Component.translatable("toast.radiante.warmup.failed"), result);
            RadianteClient.LOGGER.warn("[warmup] failed after {} ms: {}", millis, result);
        }
        state = State.FINISHED;
    }

    private static void showProgress(Minecraft minecraft) {
        int raw = RendererProxy.rebuildProgress();
        float progress;
        Component status;
        if (raw >= 1100) {
            progress = 0.99f;
            status = Component.translatable("toast.radiante.finishing");
        } else if (raw >= 1000) {
            int percent = raw - 1000;
            progress = SHADER_SHARE + (1.0f - SHADER_SHARE) * percent / 100.0f;
            status = Component.translatable("toast.radiante.pipelines", percent);
        } else {
            progress = SHADER_SHARE * raw / 100.0f;
            status = raw > 0 ? Component.translatable("toast.radiante.compiling", raw)
                : Component.translatable("toast.radiante.starting");
        }
        RebuildToast.running(minecraft, Math.min(progress, 0.99f), Component.translatable("toast.radiante.warmup"),
            status);
    }

    // ---- the fingerprint

    private static Path file() {
        return RadianteClient.radianceDir.resolve(FILE);
    }

    private static String storedFingerprint() {
        Properties properties = new Properties();
        try (InputStream in = Files.newInputStream(file())) {
            properties.load(in);
        } catch (IOException e) {
            return null;
        }
        return properties.getProperty(KEY_FINGERPRINT);
    }

    private static void remember() {
        if (currentFingerprint == null) {
            return;
        }
        Properties properties = new Properties();
        properties.setProperty(KEY_FINGERPRINT, currentFingerprint);
        properties.setProperty(KEY_VERSION, RadiantePlatform.INSTANCE.modVersion());
        try (OutputStream out = Files.newOutputStream(file())) {
            properties.store(out, "Radiante: the build whose shaders were last compiled; see PipelineWarmUp");
        } catch (IOException e) {
            RadianteClient.LOGGER.warn("[warmup] could not store the fingerprint: {}", e.toString());
        }
    }

    /**
     * A digest of what the compiled shaders depend on: the mod's version, the native library and the unpacked shader
     * files (their bytes: an edit that keeps a file's size changed nothing in a size-only digest), and the GPU with
     * its driver. Null when it cannot be worked out.
     */
    private static String fingerprint(String gpuIdentity) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            update(digest, RadiantePlatform.INSTANCE.modVersion());
            update(digest, gpuIdentity);
            Path directory = RadianteClient.radianceDir;
            for (String library : new String[] {"prismrt.dll", "libprismrt.so"}) {
                Path path = directory.resolve(library);
                if (Files.isRegularFile(path)) {
                    update(digest, library);
                    try (InputStream in = Files.newInputStream(path)) {
                        byte[] buffer = new byte[1 << 16];
                        for (int read; (read = in.read(buffer)) > 0; ) {
                            digest.update(buffer, 0, read);
                        }
                    }
                }
            }
            for (String folder : new String[] {"shaders", "modules"}) {
                Path root = directory.resolve(folder);
                if (!Files.isDirectory(root)) {
                    continue;
                }
                List<String> entries = new ArrayList<>();
                try (Stream<Path> files = Files.walk(root)) {
                    for (Path path : (Iterable<Path>) files.filter(Files::isRegularFile)::iterator) {
                        entries.add(root.relativize(path).toString().replace('\\', '/') + ":" + Files.size(path));
                    }
                }
                entries.sort(null);
                for (String entry : entries) {
                    update(digest, entry);
                    try (InputStream in = Files.newInputStream(root.resolve(entry.substring(0, entry.lastIndexOf(':'))))) {
                        byte[] buffer = new byte[1 << 16];
                        for (int read; (read = in.read(buffer)) > 0; ) {
                            digest.update(buffer, 0, read);
                        }
                    }
                }
            }
            return HexFormat.of().formatHex(digest.digest());
        } catch (IOException | NoSuchAlgorithmException e) {
            RadianteClient.LOGGER.warn("[warmup] no fingerprint: {}", e.toString());
            return null;
        }
    }

    private static void update(MessageDigest digest, String text) {
        digest.update(text.getBytes(StandardCharsets.UTF_8));
        digest.update((byte) 0);
    }
}
