package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.proxy.vulkan.RendererProxy;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.g2806.radiante.compat.VersionCompat;
import java.util.function.Consumer;
import net.minecraft.ChatFormatting;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.network.chat.Component;
import net.minecraft.util.Util;

/**
 * Brief (~0.5 s) unobstructed world pass that collects hardware GPU timestamp queries across {@link #TARGET_FRAMES}
 * frames after a short warm-up, then hands the {@link AutoTune.Result} back to {@link RadianteOptionsScreen}.
 */
final class AutoTuneScreen extends VersionCompat.DrawnScreen {

    private static final int WARMUP_FRAMES = 6;
    private static final int TARGET_FRAMES = 24;
    private static final long MAX_DURATION_MS = 2500L;

    private final Consumer<AutoTune.Result> onComplete;
    private final Runnable onCancel;

    private int startLevelFrame = -1;
    private boolean probeStarted;
    private long startMillis;
    private boolean finished;

    AutoTuneScreen(Consumer<AutoTune.Result> onComplete, Runnable onCancel) {
        super(Component.translatable("options.radiante.optimize_gpu"));
        this.onComplete = onComplete;
        this.onCancel = onCancel;
    }

    @Override
    public boolean isPauseScreen() {
        return true;
    }

    @Override
    protected boolean extractOwnBackground(GuiGraphicsExtractor graphics) {
        return true;
    }

    @Override
    protected void init() {
        if (this.startLevelFrame < 0) {
            this.startLevelFrame = RadianteRenderer.levelFrames();
            this.startMillis = Util.getMillis();
            this.probeStarted = false;
        }
    }

    @Override
    protected void extractContents(GuiGraphicsExtractor g, int mouseX, int mouseY) {
        if (this.finished) {
            return;
        }
        int elapsedFrames = Math.max(0, RadianteRenderer.levelFrames() - this.startLevelFrame);
        if (!this.probeStarted && elapsedFrames >= WARMUP_FRAMES) {
            RendererProxy.resetGpuProbe();
            this.probeStarted = true;
            this.startMillis = Util.getMillis();
        }

        int collected = this.probeStarted ? RendererProxy.gpuProbeFrames() : 0;
        boolean timedOut = (Util.getMillis() - this.startMillis) >= MAX_DURATION_MS;
        if (this.probeStarted && collected >= TARGET_FRAMES) {
            finish(RendererProxy.gpuProbeMeanMs());
            return;
        }
        if (timedOut) {
            // Too few frames timed (GPU timestamps unavailable, or the game barely drawing): the hardware estimate
            // stands in. It waited for four frames before, and with no timestamps the screen stayed up until Esc.
            finish(this.probeStarted && collected >= 4 ? RendererProxy.gpuProbeMeanMs() : 0.0f);
            return;
        }

        Component title = Component.translatable("options.radiante.optimize_gpu.measuring")
            .withStyle(ChatFormatting.BOLD);
        int shown = Math.min(collected, TARGET_FRAMES);
        Component sub = Component.translatable("options.radiante.optimize_gpu.frames", shown, TARGET_FRAMES);
        int boxW = Math.max(200, Math.max(this.font.width(title), this.font.width(sub)) + 24);
        int boxH = 38;
        int x = (this.width - boxW) / 2;
        int y = 14;

        g.fill(x, y, x + boxW, y + boxH, Theme.PANEL_DARK);
        g.outline(x, y, boxW, boxH, Theme.ACCENT);
        g.centeredText(this.font, title, x + boxW / 2, y + 6, Theme.TEXT);
        g.centeredText(this.font, sub, x + boxW / 2, y + 17, Theme.TEXT_DIM);

        int barX = x + 10;
        int barW = boxW - 20;
        int barY = y + 29;
        g.fill(barX, barY, barX + barW, barY + 4, Theme.PANEL);
        int fillW = Math.round(barW * Math.clamp((float) shown / (float) TARGET_FRAMES, 0.0f, 1.0f));
        if (fillW > 0) {
            g.fill(barX, barY, barX + fillW, barY + 4, Theme.ACCENT);
        }
    }

    private void finish(float measuredGpuMs) {
        if (this.finished || this.minecraft == null) {
            return;
        }
        this.finished = true;
        AutoTune.Probe probe = AutoTune.collectCurrentProbe(this.minecraft, measuredGpuMs);
        AutoTune.Result result = AutoTune.recommend(probe);
        this.minecraft.execute(() -> this.onComplete.accept(result));
    }

    @Override
    public void onClose() {
        if (!this.finished) {
            this.finished = true;
            this.onCancel.run();
        }
    }
}
