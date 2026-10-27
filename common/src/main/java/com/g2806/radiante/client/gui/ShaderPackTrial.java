package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.pipeline.ShaderPackLibrary;
import com.g2806.radiante.client.render.RadianteRenderer;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

import java.util.ArrayList;
import java.util.List;

/**
 * Watches a shader pack other than the built-in one until the renderer has built a pipeline with it, and puts the
 * built-in pack back when it fails. The renderer reports a pack that does not load or compile only in its error log
 * ({@code RendererProxy.drainNativeErrors}): the pack's configs.json rejected ({@code [ShaderPack] ... could not be
 * loaded}), a shader that does not compile ({@code [Shader] failed to compile ...} followed by the compiler's
 * messages), or the rebuild failing for another reason. Those lines are kept for {@link ShaderPacksScreen} to show,
 * since a pack author needs the compiler's message rather than a black world.
 *
 * <p>A trial ends well once the renderer has logged a finished rebuild ({@code [Rebuild] total}) and then drawn the
 * world for ten seconds without an error: a pack can compile and still lose the GPU with its first frames, as
 * Radiance's Vanilla PT does. With Radiante's debug log off that line never comes, so a minute of drawing the world
 * without an error counts as well. Meanwhile a marker file names the pack ({@link ShaderPackLibrary#markTrial}), so a
 * crash during the trial puts the built-in pack back at the next start instead of crashing again. A pack chosen in an
 * earlier session is tried again on the first world of this one.
 */
public final class ShaderPackTrial {

    /** Ticks of drawing a world without an error after which a pack counts as working. */
    private static final int QUIET_TICKS = 20 * 60;
    /** The same once the renderer said the pipeline was built. */
    private static final int QUIET_TICKS_AFTER_BUILD = 20 * 10;
    /** Lines of the renderer's log kept for the report. */
    private static final int KEPT_LINES = 80;

    /** Why the last pack tried did not work: its name and what the renderer said. */
    public record Failure(String packName, String packPath, List<String> lines) {
    }

    private static String trialPath;
    private static String trialName;
    private static final List<String> lines = new ArrayList<>();
    private static boolean failed;
    private static boolean succeeded;
    private static int quietTicks;
    /** The pack that already worked this session; it is not tried again. */
    private static String verifiedPath;
    private static Failure lastFailure;

    private ShaderPackTrial() {
    }

    /** Starts watching a pack that was just chosen. The built-in pack is not watched: there is nothing to go back to. */
    static void begin(String path, String name) {
        lastFailure = null;
        if (path == null || ShaderPackLibrary.samePack(path, Pipeline.builtInShaderPackPath())) {
            trialPath = null;
            ShaderPackLibrary.markTrial(null);
            return;
        }
        trialPath = path;
        trialName = name;
        lines.clear();
        failed = false;
        succeeded = false;
        quietTicks = 0;
        verifiedPath = null;
        ShaderPackLibrary.markTrial(path);
    }

    /** Whether a pack is being watched now, for the screen to say it is still being tried. */
    static boolean trying(String path) {
        return trialPath != null && ShaderPackLibrary.samePack(trialPath, path);
    }

    /** The last pack that failed (in this session, or by ending the last one), until another is chosen. */
    static Failure lastFailure() {
        if (lastFailure == null) {
            String crashed = ShaderPackLibrary.takeCrashedTrial();
            if (crashed != null) {
                lastFailure = new Failure(crashed.substring(crashed.replace('\\', '/').lastIndexOf('/') + 1), crashed,
                    List.of(Component.translatable("screen.radiante.shader_packs.crashed").getString()));
            }
        }
        return lastFailure;
    }

    /** Every line the renderer logs, before RadianteClient folds repeats together. */
    public static void onNativeLine(String line, boolean error) {
        if (trialPath == null || line == null || line.isBlank()) {
            return;
        }
        if (!error) {
            if (line.startsWith("[Rebuild] total")) {
                succeeded = true;
            }
            return;
        }
        if (lines.size() < KEPT_LINES) {
            lines.add(line);
        }
        if (line.contains("[ShaderPack]") && line.contains("could not be loaded")
            || line.contains("failed to compile shader source") || line.contains("does not compile")
            || line.contains("pipeline rebuild failed") || line.contains("renderFrame failed")
            || line.contains("prewarm") && line.contains("failed")) {
            failed = true;
        }
    }

    /** Every client tick, after the renderer's log was read. */
    public static void tick(Minecraft minecraft) {
        if (trialPath == null) {
            startForStoredPack();
            return;
        }
        if (failed) {
            fallBack(minecraft);
            return;
        }
        // Counted while the world is drawn, behind the shader pack screen too.
        if (RadianteRenderer.isActive() && minecraft.level != null
            && ++quietTicks >= (succeeded ? QUIET_TICKS_AFTER_BUILD : QUIET_TICKS)) {
            finish();
        }
    }

    /** A pack kept from an earlier session is watched like a newly chosen one, once the renderer runs. */
    private static void startForStoredPack() {
        if (!RadianteRenderer.isActive()) {
            return;
        }
        String stored = Pipeline.shaderPackPath();
        if (stored == null || ShaderPackLibrary.samePack(stored, Pipeline.builtInShaderPackPath())
            || ShaderPackLibrary.samePack(stored, verifiedPath)
            || lastFailure != null && ShaderPackLibrary.samePack(stored, lastFailure.packPath())) {
            return;
        }
        String name = stored.substring(stored.replace('\\', '/').lastIndexOf('/') + 1);
        begin(stored, name);
    }

    private static void finish() {
        if (Options.debugLogging) {
            RadianteClient.LOGGER.info("[shader packs] {} works", trialPath);
        }
        verifiedPath = trialPath;
        trialPath = null;
        ShaderPackLibrary.markTrial(null);
    }

    private static void fallBack(Minecraft minecraft) {
        lastFailure = new Failure(trialName, trialPath, List.copyOf(lines));
        RadianteClient.LOGGER.warn("Shader pack {} failed; going back to the built-in pack", trialPath);
        trialPath = null;
        ShaderPackLibrary.markTrial(null);
        Pipeline.selectShaderPack(Pipeline.builtInShaderPackPath());
        Screen current = minecraft.gui.screen();
        Screen next = current instanceof ShaderPacksScreen packs ? packs.reopened() : current;
        // The settings screen is not replaced, so it keeps its unapplied choices: the built-in pack is built here
        // without a wait screen, and the next frame compiles it (from the caches, as it ran before).
        if (current instanceof RadianteOptionsScreen) {
            if (!BackgroundRebuild.start()) {
                Pipeline.build();
            }
        } else {
            BackgroundRebuild.open(minecraft, next, true);
        }
        RebuildToast.failed(minecraft, Component.translatable("toast.radiante.shader_pack_failed"),
            Component.translatable("toast.radiante.shader_pack_failed.detail", trialName).getString());
    }
}
