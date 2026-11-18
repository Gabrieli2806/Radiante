package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.render.RadianteRenderer;
import com.g2806.radiante.compat.VersionCompat;
import com.mojang.blaze3d.pipeline.RenderTarget;
import com.mojang.blaze3d.platform.InputConstants;
import com.mojang.blaze3d.platform.NativeImage;
import net.minecraft.ChatFormatting;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Screenshot;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;

/**
 * Interactive Before / After split-screen preview for {@link RadianteOptionsScreen}.
 *
 * <p>Captures the un-blurred traced world on demand when entering Before / After (before staged changes are applied)
 * or when explicitly pressing Set Before, without copying the screen during normal settings visits and without
 * discarding the captured "Before" reference if the camera moves during the session.
 */
public final class BeforeAfterPreview {

    private static boolean sessionOpen;
    private static boolean baselineRequested;
    private static int waitFrames;
    private static Runnable afterBaselineCallback;
    private static boolean active;
    private static boolean hudOverridden;
    private static boolean hudWasHidden;
    private static float splitFraction = 0.5f;
    private static boolean draggingSplit;

    private static Identifier textureId;
    private static int generation;
    private static int captureCount;
    private static int texWidth;
    private static int texHeight;
    private static Object capturedLevel;

    private BeforeAfterPreview() {
    }

    /** Opens or validates the settings session without copying the screen until Compare or Set Before is used. */
    public static void ensureSession(Minecraft minecraft) {
        if (minecraft == null || minecraft.level == null || !RadianteRenderer.isRayTracingEnabled()) {
            onSessionClose();
            return;
        }
        if (sessionOpen && capturedLevel != null && capturedLevel != minecraft.level) {
            releaseTexture();
            baselineRequested = false;
            afterBaselineCallback = null;
        }
        sessionOpen = true;
        capturedLevel = minecraft.level;
    }

    public static boolean hasSession() {
        return sessionOpen;
    }

    public static boolean matchesLevel(Object level) {
        return level != null && level == capturedLevel;
    }

    public static int captureCount() {
        return captureCount;
    }

    /** Captures the current live world frame as the new "Before" reference on the next rendered frame. */
    public static void requestRebaseline() {
        afterBaselineCallback = null;
        baselineRequested = true;
        waitFrames = 1;
    }

    /**
     * Captures the current un-applied world frame as "Before" on the next rendered frame, then runs {@code onCaptured}
     * on the client thread so staged changes apply only after the baseline is taken.
     */
    public static void captureBeforeAndThen(Runnable onCaptured) {
        afterBaselineCallback = onCaptured;
        baselineRequested = true;
        waitFrames = 0;
    }

    /** Ends the settings session and frees the snapshot texture. */
    public static void onSessionClose() {
        setActive(false);
        sessionOpen = false;
        baselineRequested = false;
        afterBaselineCallback = null;
        draggingSplit = false;
        capturedLevel = null;
        releaseTexture();
    }

    public static boolean isActive() {
        return active;
    }

    public static boolean wasHudHiddenBeforeCompare() {
        return hudWasHidden;
    }

    public static void setActive(boolean enabled) {
        Minecraft mc = Minecraft.getInstance();
        if (enabled) {
            if (!hudOverridden && mc != null && mc.gui != null && mc.gui.hud != null) {
                hudWasHidden = mc.gui.hud.isHidden();
                if (!hudWasHidden) {
                    mc.gui.hud.toggle();
                }
                hudOverridden = true;
            }
        } else if (hudOverridden) {
            if (mc != null && mc.gui != null && mc.gui.hud != null && mc.gui.hud.isHidden() != hudWasHidden) {
                mc.gui.hud.toggle();
            }
            hudOverridden = false;
        }
        active = enabled;
        draggingSplit = false;
    }

    public static float splitFraction() {
        return splitFraction;
    }

    public static void setSplitFraction(float fraction) {
        splitFraction = Math.clamp(fraction, 0.05f, 0.95f);
    }

    public static boolean hasBaseline() {
        return textureId != null && texWidth > 0 && texHeight > 0;
    }

    /**
     * Called at the end of {@code GameRenderer.renderLevel} before any GUI or screen background blur is drawn onto
     * {@code mainTarget}. Inert unless a baseline capture was requested.
     */
    public static void onWorldFrameRendered(RenderTarget mainTarget) {
        if (active) {
            Minecraft mc = Minecraft.getInstance();
            var screen = mc.gui != null ? mc.gui.screen() : null;
            if (!(screen instanceof RadianteOptionsScreen) && !(screen instanceof ApplyingSettingsScreen)) {
                setActive(false);
            }
        }
        if (!baselineRequested || mainTarget == null || mainTarget.getColorTexture() == null) {
            return;
        }
        if (waitFrames > 0) {
            waitFrames--;
            return;
        }
        baselineRequested = false;
        captureCount++;
        try {
            Screenshot.takeScreenshot(mainTarget, image -> {
                Minecraft mc = Minecraft.getInstance();
                if (mc.isSameThread()) {
                    installBaseline(image);
                } else {
                    mc.execute(() -> installBaseline(image));
                }
            });
        } catch (RuntimeException e) {
            RadianteClient.LOGGER.warn("Could not capture Before/After baseline frame", e);
            Runnable callback = afterBaselineCallback;
            afterBaselineCallback = null;
            if (callback != null) {
                callback.run();
            }
        }
    }

    private static void installBaseline(NativeImage image) {
        if (!sessionOpen) {
            afterBaselineCallback = null;
            image.close();
            return;
        }
        Runnable callback = afterBaselineCallback;
        afterBaselineCallback = null;
        NativeImage pending = image;
        try {
            int w = pending.getWidth();
            int h = pending.getHeight();
            // Ensure full opacity on every pixel so GUI_TEXTURED never alpha-blends the live After world through sky or water.
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    pending.setPixel(x, y, pending.getPixel(x, y) | 0xFF000000);
                }
            }
            Identifier previous = textureId;
            Identifier next = Identifier.fromNamespaceAndPath("radiante", "before_after/" + (++generation));
            if (VersionCompat.registerImageTexture(next, () -> "Radiante before/after baseline", pending)) {
                pending = null;
            }
            textureId = next;
            texWidth = w;
            texHeight = h;
            if (previous != null) {
                Minecraft.getInstance().getTextureManager().release(previous);
            }
        } catch (RuntimeException e) {
            RadianteClient.LOGGER.warn("Could not register Before/After baseline texture", e);
        } finally {
            if (pending != null) {
                pending.close();
            }
        }
        if (callback != null) {
            callback.run();
        }
    }

    private static void releaseTexture() {
        if (textureId != null) {
            Identifier old = textureId;
            textureId = null;
            texWidth = 0;
            texHeight = 0;
            try {
                Minecraft.getInstance().getTextureManager().release(old);
            } catch (RuntimeException ignored) {
            }
        }
    }

    private record TopBarRects(int backX, int backW, int resetX, int resetW, int setBeforeX, int setBeforeW, int barY) {
    }

    private static TopBarRects topBar(Font font, int screenW) {
        Component backLabel = Component.translatable("options.radiante.compare.exit");
        Component resetLabel = Component.literal("50%");
        Component setBeforeLabel = Component.translatable("options.radiante.compare.set_before");
        int backW = font.width(backLabel) + 16;
        int resetW = font.width(resetLabel) + 14;
        int setBeforeW = font.width(setBeforeLabel) + 16;
        int totalW = backW + 4 + resetW + 4 + setBeforeW;
        int startX = (screenW - totalW) / 2;
        return new TopBarRects(startX, backW, startX + backW + 4, resetW, startX + backW + 4 + resetW + 4, setBeforeW, 10);
    }

    static void extract(GuiGraphicsExtractor g, Font font, int screenW, int screenH, int mouseX, int mouseY) {
        int splitX = splitPixel(screenW);

        // Left side: captured "Before" frame. Right side stays uncovered so the live traced "After" world shows directly.
        if (hasBaseline()) {
            g.enableScissor(0, 0, splitX, screenH);
            VersionCompat.blit(g, textureId, 0, 0, 0.0f, 0.0f, screenW, screenH, texWidth, texHeight, texWidth, texHeight);
            g.disableScissor();
        }

        // Vertical split line with soft dark shadow on both sides so it reads cleanly against bright sky or dark caves.
        boolean nearLine = draggingSplit || Math.abs(mouseX - splitX) <= 10;
        int lineColor = nearLine ? Theme.ACCENT : 0xFFE6FAF5;
        g.fill(splitX - 2, 0, splitX - 1, screenH, 0x90000000);
        g.fill(splitX + 1, 0, splitX + 2, screenH, 0x90000000);
        g.fill(splitX - 1, 0, splitX + 1, screenH, lineColor);

        // Center grab handle.
        int handleY = screenH / 2 - 11;
        int handleW = 34;
        int handleH = 22;
        int handleX = splitX - handleW / 2;
        g.fill(handleX, handleY, handleX + handleW, handleY + handleH, nearLine ? Theme.PANEL_DARK : Theme.PANEL);
        g.outline(handleX, handleY, handleW, handleH, lineColor);
        g.centeredText(font, Component.literal("◄ │ ►"), splitX, handleY + 7, lineColor);

        // BEFORE / AFTER corner badges.
        int margin = Math.max(8, screenW / 32);
        Component beforeText = Component.translatable("options.radiante.compare.before").withStyle(ChatFormatting.BOLD);
        Component afterText = Component.translatable("options.radiante.compare.after").withStyle(ChatFormatting.BOLD);
        int beforeW = font.width(beforeText) + 14;
        int afterW = font.width(afterText) + 14;
        int badgeY = screenH - 28;
        if (splitX > margin + beforeW + 6) {
            g.fill(margin, badgeY, margin + beforeW, badgeY + 18, Theme.PANEL);
            g.outline(margin, badgeY, beforeW, 18, 0x60FFFFFF);
            g.centeredText(font, beforeText, margin + beforeW / 2, badgeY + 5, Theme.TEXT);
        }
        if (screenW - splitX > margin + afterW + 6) {
            int ax = screenW - margin - afterW;
            g.fill(ax, badgeY, ax + afterW, badgeY + 18, Theme.PANEL);
            g.outline(ax, badgeY, afterW, 18, Theme.ACCENT);
            g.centeredText(font, afterText, ax + afterW / 2, badgeY + 5, Theme.ACCENT);
        }

        // Top control bar.
        TopBarRects bar = topBar(font, screenW);
        drawPillButton(g, font, Component.translatable("options.radiante.compare.exit"), bar.backX(), bar.barY(),
            bar.backW(), mouseX, mouseY);
        drawPillButton(g, font, Component.literal("50%"), bar.resetX(), bar.barY(), bar.resetW(), mouseX, mouseY);
        drawPillButton(g, font, Component.translatable("options.radiante.compare.set_before"), bar.setBeforeX(),
            bar.barY(), bar.setBeforeW(), mouseX, mouseY);

        // Subtle bottom-center hint when wide enough.
        Component hint = Component.translatable("options.radiante.compare.hint");
        int hintW = font.width(hint) + 16;
        if (hintW + beforeW + afterW + margin * 4 < screenW) {
            int hx = (screenW - hintW) / 2;
            g.fill(hx, badgeY + 1, hx + hintW, badgeY + 17, 0xA8101413);
            g.centeredText(font, hint, screenW / 2, badgeY + 5, Theme.TEXT_DIM);
        }
    }

    private static void drawPillButton(GuiGraphicsExtractor g, Font font, Component label, int x, int y, int w,
                                       int mouseX, int mouseY) {
        boolean hover = mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + 18;
        g.fill(x, y, x + w, y + 18, hover ? Theme.PANEL_DARK : Theme.PANEL);
        g.outline(x, y, w, 18, hover ? Theme.ACCENT : 0x55FFFFFF);
        g.centeredText(font, label, x + w / 2, y + 5, hover ? Theme.ACCENT : Theme.TEXT);
    }

    static boolean mouseClicked(double mouseX, double mouseY, int button, Font font, int screenW,
                                Runnable onExit, Runnable onRebaseline) {
        if (button != 0) {
            return false;
        }
        TopBarRects bar = topBar(font, screenW);
        if (mouseY >= bar.barY() && mouseY < bar.barY() + 18) {
            if (mouseX >= bar.backX() && mouseX < bar.backX() + bar.backW()) {
                onExit.run();
                return true;
            }
            if (mouseX >= bar.resetX() && mouseX < bar.resetX() + bar.resetW()) {
                setSplitFraction(0.5f);
                return true;
            }
            if (mouseX >= bar.setBeforeX() && mouseX < bar.setBeforeX() + bar.setBeforeW()) {
                onRebaseline.run();
                return true;
            }
        }
        draggingSplit = true;
        setSplitFraction((float) (mouseX / Math.max(1, screenW)));
        return true;
    }

    static boolean mouseDragged(double mouseX, int screenW) {
        if (!draggingSplit) {
            return false;
        }
        setSplitFraction((float) (mouseX / Math.max(1, screenW)));
        return true;
    }

    static void mouseReleased() {
        draggingSplit = false;
    }

    static boolean keyPressed(KeyEvent event, Runnable onExit) {
        if (event.key() == InputConstants.KEY_ESCAPE) {
            onExit.run();
            return true;
        }
        if (event.isLeft() || event.isRight()) {
            float step = event.hasShiftDown() ? 0.10f : 0.025f;
            setSplitFraction(splitFraction + (event.isLeft() ? -step : step));
            return true;
        }
        return false;
    }

    private static int splitPixel(int screenW) {
        int pad = Math.min(24, Math.max(8, screenW / 20));
        return Math.clamp(Math.round(splitFraction * screenW), pad, Math.max(pad, screenW - pad));
    }
}
