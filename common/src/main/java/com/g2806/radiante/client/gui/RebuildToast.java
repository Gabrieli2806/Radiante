package com.g2806.radiante.client.gui;

import com.google.common.collect.ImmutableList;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.toasts.Toast;
import net.minecraft.client.gui.components.toasts.ToastManager;
import net.minecraft.client.renderer.RenderPipelines;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.util.FormattedCharSequence;

/**
 * The corner popup that shows a settings change being applied: the mod's icon, a title, a status line and a thin
 * progress bar while the shaders compile, then "applied" or the error. A vanilla toast (the kind advancements use), so
 * it slides in at the top right on every screen - the title screen included - and never blocks the menus. One toast
 * is kept and updated as the work goes on, so a second change before the first is done reuses it.
 */
final class RebuildToast implements Toast {

    private static final Identifier BACKGROUND_SPRITE = Identifier.withDefaultNamespace("toast/advancement");
    private static final Identifier ICON =
        Identifier.fromNamespaceAndPath("radiante", "textures/gui/settings/icon.png");
    private static final int ICON_SOURCE_SIZE = 32;
    private static final int ICON_SIZE = 20;
    private static final int WIDTH = 200;
    private static final int TEXT_X = 30;
    private static final int TEXT_WIDTH = WIDTH - TEXT_X - 6;
    private static final int LINE_HEIGHT = 12;
    private static final int MAX_MESSAGE_LINES = 4;
    private static final int TITLE_COLOR = 0xFFFFFF00;
    private static final int FAILED_TITLE_COLOR = 0xFFFF5555;
    private static final int MESSAGE_COLOR = 0xFFFFFFFF;
    private static final int BAR_COLOR = 0xFF8FE3C8;
    private static final int BAR_BACKGROUND = 0xFF000000;
    /** How long the "applied" or failed state stays after the work ended (scaled by the player's notification time). */
    private static final long RESULT_MS = 4000L;
    private static final long FAILED_MS = 12000L;

    /** Which part of the work the toast shows. */
    enum State {
        RUNNING, DONE, FAILED
    }

    private static final long EXPIRED = -3L;
    private static final long JUST_SET = -2L;

    private State state = State.RUNNING;
    private Component title = Component.empty();
    private List<FormattedCharSequence> message = ImmutableList.of();
    private float progress;
    /** Smoothed value drawn, so the bar moves rather than jumps. */
    private float shownProgress;
    private long lastUpdate = -1L;
    private long resultAt = -1L;

    private RebuildToast() {
    }

    /** The toast being shown, or a new one queued. */
    private static RebuildToast get(Minecraft minecraft) {
        ToastManager manager = minecraft.gui.toastManager();
        RebuildToast toast = manager.getToast(RebuildToast.class, Toast.NO_TOKEN);
        if (toast == null) {
            toast = new RebuildToast();
            manager.addToast(toast);
        }
        return toast;
    }

    /** Applying: {@code progress} 0..1, {@code status} the line under the title. */
    static void running(Minecraft minecraft, float progress, Component status) {
        running(minecraft, progress, Component.translatable("toast.radiante.applying"), status);
    }

    /** The same under a title of the caller's own (the start-up warm-up has its own). */
    static void running(Minecraft minecraft, float progress, Component title, Component status) {
        RebuildToast toast = get(minecraft);
        toast.state = State.RUNNING;
        toast.resultAt = -1L;
        toast.title = title;
        toast.setMessage(minecraft.font, status);
        toast.progress = Math.max(0.0f, Math.min(1.0f, progress));
    }

    static void done(Minecraft minecraft) {
        done(minecraft, Component.translatable("toast.radiante.done"), Component.translatable("toast.radiante.done.detail"));
    }

    static void done(Minecraft minecraft, Component title, Component detail) {
        RebuildToast toast = get(minecraft);
        toast.state = State.DONE;
        toast.title = title;
        toast.setMessage(minecraft.font, detail);
        toast.progress = 1.0f;
        toast.resultAt = JUST_SET;
    }

    static void failed(Minecraft minecraft, String reason) {
        failed(minecraft, Component.translatable("toast.radiante.failed"), reason);
    }

    static void failed(Minecraft minecraft, Component title, String reason) {
        RebuildToast toast = get(minecraft);
        toast.state = State.FAILED;
        toast.title = title;
        Component detail = reason == null || reason.isBlank() ? Component.translatable("toast.radiante.failed.detail")
            : Component.translatable("toast.radiante.failed.reason", reason);
        toast.setMessage(minecraft.font, detail);
        toast.resultAt = JUST_SET;
    }

    private void setMessage(Font font, Component text) {
        List<FormattedCharSequence> lines = font.split(text, TEXT_WIDTH);
        this.message = lines.size() > MAX_MESSAGE_LINES ? lines.subList(0, MAX_MESSAGE_LINES) : lines;
    }

    @Override
    public int width() {
        return WIDTH;
    }

    @Override
    public int height() {
        return 26 + Math.max(this.message.size(), 1) * LINE_HEIGHT;
    }

    @Override
    public Toast.Visibility getWantedVisibility() {
        return this.state != State.RUNNING && this.resultAt == EXPIRED ? Toast.Visibility.HIDE : Toast.Visibility.SHOW;
    }

    @Override
    public void update(ToastManager manager, long fullyVisibleForMs) {
        if (this.state == State.RUNNING) {
            this.resultAt = -1L;
        } else if (this.resultAt == JUST_SET) {
            // The result was just set: its time starts now.
            this.resultAt = fullyVisibleForMs;
        } else if (this.resultAt >= 0L) {
            long shownFor = (long) ((this.state == State.FAILED ? FAILED_MS : RESULT_MS)
                * manager.getNotificationDisplayTimeMultiplier());
            if (fullyVisibleForMs - this.resultAt >= shownFor) {
                this.resultAt = EXPIRED;
            }
        }
        long step = this.lastUpdate < 0L ? 0L : fullyVisibleForMs - this.lastUpdate;
        this.lastUpdate = fullyVisibleForMs;
        this.shownProgress += (this.progress - this.shownProgress) * Math.min(1.0f, step / 150.0f + 0.05f);
    }

    @Override
    public void extractRenderState(GuiGraphicsExtractor graphics, Font font, long fullyVisibleForMs) {
        int height = this.height();
        graphics.blitSprite(RenderPipelines.GUI_TEXTURED, BACKGROUND_SPRITE, 0, 0, WIDTH, height);
        graphics.blit(RenderPipelines.GUI_TEXTURED, ICON, 6, (height - ICON_SIZE) / 2, 0.0f, 0.0f, ICON_SIZE,
            ICON_SIZE, ICON_SOURCE_SIZE, ICON_SOURCE_SIZE, ICON_SOURCE_SIZE, ICON_SOURCE_SIZE);
        graphics.text(font, this.title, TEXT_X, 7, this.state == State.FAILED ? FAILED_TITLE_COLOR : TITLE_COLOR,
            false);
        for (int i = 0; i < this.message.size(); i++) {
            graphics.text(font, this.message.get(i), TEXT_X, 7 + (i + 1) * LINE_HEIGHT, MESSAGE_COLOR, false);
        }
        if (this.state != State.FAILED) {
            int barWidth = WIDTH - TEXT_X - 8;
            int y = height - 6;
            graphics.fill(TEXT_X, y, TEXT_X + barWidth, y + 2, BAR_BACKGROUND);
            graphics.fill(TEXT_X, y, TEXT_X + (int) (barWidth * this.shownProgress), y + 2, BAR_COLOR);
        }
    }
}
