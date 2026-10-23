package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.pipeline.Pipeline;
import com.g2806.radiante.client.render.RadianteRenderer;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

/**
 * Applies a settings change that needs the pipeline built again without stopping the game: the renderer compiles the
 * new shaders and ray tracing pipelines in the background ({@link Pipeline#prewarm()}) while the world keeps drawing
 * with the old settings and a popup in the top right corner ({@link RebuildToast}) shows the progress; then the
 * ordinary build runs, which finds everything compiled and costs a short hitch instead of seconds behind
 * {@link ApplyingSettingsScreen}. Outside a world nothing is drawn by the renderer, so there is nothing to keep
 * running: the build only describes the new pipeline to the renderer, which compiles it with the first frame of the
 * next world (or in the background before that, when a world was visited already), and the popup reports it. The
 * wait screen is left for a world that is open and cannot be prewarmed (another shader pack, a failed compile).
 */
public final class BackgroundRebuild {

    /** A rebuild between (a window resize) stops a prewarm; it is started again this many times at most. */
    private static final int MAX_RESTARTS = 3;
    /** Shares of the progress line: the driver's pipeline compile takes most of the time. */
    private static final float SHADER_SHARE = 0.3f;

    private static final int[] STATUS = new int[5];
    private static boolean active;
    /** The prewarm could not finish: the wait screen opens once no settings screen is in the way. */
    private static boolean fallbackPending;
    private static int restarts;

    private BackgroundRebuild() {
    }

    /**
     * Starts applying the pipeline change in the background; the caller opens its next screen at once. False when
     * that is not possible: the caller shows {@link ApplyingSettingsScreen} instead. Starting again while one runs
     * restarts it with the newest values.
     */
    static boolean start() {
        if (!RadianteRenderer.isActive() || !Pipeline.prewarm()) {
            return false;
        }
        active = true;
        RebuildToast.running(Minecraft.getInstance(), 0.0f, Component.translatable("toast.radiante.starting"));
        fallbackPending = false;
        restarts = 0;
        if (Options.debugLogging) {
            RadianteClient.LOGGER.info("[settings] compiling the new pipeline in the background");
        }
        return true;
    }

    /** Opens the screen next, through the wait screen only when the change cannot be applied in the background. */
    static void open(Minecraft minecraft, Screen next, boolean rebuild) {
        if (rebuild && !start()) {
            if (minecraft.level == null) {
                buildNow(minecraft);
            } else {
                next = new ApplyingSettingsScreen(next);
            }
        }
        minecraft.gui.setScreen(next);
    }

    /**
     * The build outside a world: it only hands the description to the renderer (the compile waits for a frame, which
     * only a world draws), so it is quick and needs no screen. The popup tells how it went.
     */
    private static void buildNow(Minecraft minecraft) {
        active = false;
        fallbackPending = false;
        Pipeline.build();
        finish(minecraft);
    }

    /** The popup's last state for a build that just ran: applied, or why it was not. */
    static void finish(Minecraft minecraft) {
        if (Pipeline.takeStructuralChange()) {
            RebuildToast.failed(minecraft, Pipeline.lastBuildError());
        } else {
            RebuildToast.done(minecraft);
        }
    }

    /** Every client tick: follows the prewarm and builds once it is done. */
    public static void tick(Minecraft minecraft) {
        if (fallbackPending) {
            openWaitScreen(minecraft);
            return;
        }
        if (!active) {
            return;
        }
        if (!RadianteRenderer.isActive()) {
            // The change stays pending (Pipeline.takeStructuralChange): the build that starts the renderer again
            // carries it.
            active = false;
            return;
        }
        try {
            Pipeline.prewarmStatus(STATUS);
        } catch (UnsatisfiedLinkError olderRenderer) {
            STATUS[0] = Pipeline.PREWARM_FAILED;
        }
        switch (STATUS[0]) {
            case Pipeline.PREWARM_RUNNING -> showProgress(minecraft);
            case Pipeline.PREWARM_DONE -> {
                active = false;
                // A build since (another screen's, a dev command's) already carried the change.
                if (Pipeline.takeStructuralChange()) {
                    Pipeline.build();
                    finish(minecraft);
                } else {
                    RebuildToast.done(minecraft);
                }
            }
            case Pipeline.PREWARM_CANCELLED -> {
                if (!Pipeline.takeStructuralChange()) {
                    active = false;
                } else if (restarts >= MAX_RESTARTS || !Pipeline.prewarm()) {
                    fallBack();
                } else {
                    restarts++;
                }
            }
            // Failed, or no prewarm at all (the renderer was started again).
            default -> fallBack();
        }
    }

    private static void fallBack() {
        active = false;
        fallbackPending = Pipeline.takeStructuralChange();
        if (fallbackPending && Minecraft.getInstance().level == null) {
            // No world to keep playing in, so no wait screen: build now.
            buildNow(Minecraft.getInstance());
        }
    }

    /**
     * The wait screen over whatever is open, except a settings screen: replacing it would drop its unapplied choices,
     * and applying them starts the change again anyway.
     */
    private static void openWaitScreen(Minecraft minecraft) {
        Screen current = minecraft.gui.screen();
        if (current instanceof RadianteOptionsScreen || current instanceof ShareSettingsScreen
            || current instanceof ApplyingSettingsScreen) {
            return;
        }
        fallbackPending = false;
        if (Pipeline.takeStructuralChange() && !active) {
            minecraft.gui.setScreen(new ApplyingSettingsScreen(current));
        }
    }

    private static void showProgress(Minecraft minecraft) {
        float shaders = STATUS[2] > 0 ? STATUS[1] / (float) STATUS[2] : 0.0f;
        float pipelines = STATUS[4] > 0 ? STATUS[3] / (float) STATUS[4] : 0.0f;
        int percent = Math.min(99, (int) (100.0f * (SHADER_SHARE * shaders + (1.0f - SHADER_SHARE) * pipelines)));
        Component status = shaders < 1.0f ? Component.translatable("toast.radiante.compiling", percent)
            : Component.translatable("toast.radiante.pipelines", percent);
        RebuildToast.running(minecraft, percent / 100.0f, status);
    }
}
