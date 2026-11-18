package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.RadianteClient;
import com.g2806.radiante.client.pipeline.ShaderPackLibrary;
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
    /** Where the divider is drawn, easing towards splitFraction so 50% and the arrow keys slide it there. */
    private static float shownFraction = 0.5f;
    private static boolean draggingSplit;
    /** Space held: the whole screen shows Before, to flick between the two. */
    private static boolean peeking;
    /** Before is the picture with ray tracing off (Radiante Off / On), not the settings as they were. */
    private static boolean beforeIsOff;
    /** Set on entering Before / After: the Off picture is taken as soon as the comparison shows. */
    private static boolean offOnEnter;

    /** Before will be the game without Radiante, taken when the comparison opens. */
    public static void takeOffOnEnter() {
        offOnEnter = true;
    }
    /** The shader pack the Before picture was taken with, to name the halves when the pack was changed since. */
    private static String beforePack;
    /** Off / On capture in progress: ray tracing is off until the vanilla picture is taken (nanoTime deadline). */
    private static boolean offCapturePending;
    private static long offCaptureAt;
    /** After an Off / On capture, until ray tracing has the world built again (nanoTime): the screen stays covered. */
    private static long coverUntil;
    private static final long ON_SETTLE_NANOS = 1_500_000_000L;
    /** How long the game's own renderer gets to build the world again before its picture is taken. */
    private static final long OFF_SETTLE_NANOS = 2_000_000_000L;

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
        // Ray tracing is off on purpose while an Off / On capture runs: that is no reason to end the session.
        if (offCapturePending && minecraft != null && minecraft.level != null) {
            return;
        }
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
        beforeIsOff = false;
    }

    /**
     * Takes Before with ray tracing off, so the two halves compare Radiante off and on. The game's own renderer has
     * to build the world again first, so ray tracing stays off for a moment and then comes back on.
     */
    public static void requestOffBaseline() {
        Minecraft mc = Minecraft.getInstance();
        if (offCapturePending || mc == null || mc.level == null || !RadianteRenderer.isRayTracingEnabled()) {
            RadianteClient.LOGGER.info("Before / After: Radiante Off / On not started (pending {}, world {}, traced {})",
                offCapturePending, mc != null && mc.level != null, RadianteRenderer.isRayTracingEnabled());
            return;
        }
        RadianteClient.LOGGER.info("Before / After: taking the picture with ray tracing off");
        ensureSession(mc);
        offCapturePending = true;
        offCaptureAt = System.nanoTime() + OFF_SETTLE_NANOS;
        RadianteClient.toggleRayTracing(mc);
    }

    public static boolean offCapturePending() {
        return offCapturePending;
    }

    /**
     * Captures the current un-applied world frame as "Before" on the next rendered frame, then runs {@code onCaptured}
     * on the client thread so staged changes apply only after the baseline is taken.
     */
    public static void captureBeforeAndThen(Runnable onCaptured) {
        beforeIsOff = false;
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
        peeking = false;
        offOnEnter = false;
        capturedLevel = null;
        releaseTexture();
        finishOffCapture();
    }

    /** Ray tracing back on after an Off / On capture, whichever way it ended. */
    private static void finishOffCapture() {
        if (!offCapturePending) {
            return;
        }
        offCapturePending = false;
        coverUntil = System.nanoTime() + ON_SETTLE_NANOS;
        Minecraft mc = Minecraft.getInstance();
        if (mc != null && !com.g2806.radiante.client.option.Options.rayTracingEnabled) {
            RadianteClient.toggleRayTracing(mc);
        }
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
        splitFraction = Math.clamp(fraction, 0.0f, 1.0f);
    }

    /** Moves the divider at once, as dragging does, instead of sliding it there. */
    private static void dragTo(float fraction) {
        setSplitFraction(fraction);
        shownFraction = splitFraction;
    }

    public static boolean hasBaseline() {
        return textureId != null && texWidth > 0 && texHeight > 0;
    }

    /**
     * Called at the end of {@code GameRenderer.renderLevel} before any GUI or screen background blur is drawn onto
     * {@code mainTarget}. Inert unless a baseline capture was requested.
     */
    public static void onWorldFrameRendered(RenderTarget mainTarget) {
        // Entering Before / After: Before is the game without Radiante, taken once the comparison is on screen (the
        // settings applied and their pipeline built), not in the middle of the build.
        if (offOnEnter && active && !offCapturePending
            && Minecraft.getInstance().gui.screen() instanceof RadianteOptionsScreen) {
            offOnEnter = false;
            requestOffBaseline();
        }
        if (offCapturePending && !baselineRequested && System.nanoTime() >= offCaptureAt) {
            // The vanilla world has had its time: take it, then hand the world back to ray tracing.
            afterBaselineCallback = () -> {
                beforeIsOff = true;
                finishOffCapture();
            };
            baselineRequested = true;
            waitFrames = 0;
            // Once: the picture reaches installBaseline a frame or more later, and until then this would ask again.
            offCaptureAt = Long.MAX_VALUE;
        }
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
            // Ray tracing comes back on whatever became of the picture: left off, the game stayed without it.
            finishOffCapture();
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
            beforePack = com.g2806.radiante.client.pipeline.PipelineShaderPack.shaderPackPath();
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

    private record TopBarRects(int backX, int backW, int resetX, int resetW, int setBeforeX, int setBeforeW,
                               int offX, int offW, int packX, int packW, int barY) {
    }

    private static Component packLabel() {
        return Component.translatable("options.radiante.compare.pack",
            packName(com.g2806.radiante.client.pipeline.PipelineShaderPack.shaderPackPath()));
    }

    private static TopBarRects topBar(Font font, int screenW) {
        int backW = font.width(Component.translatable("options.radiante.compare.exit")) + 16;
        int resetW = font.width(Component.literal("50%")) + 14;
        // Before is always the game without Radiante, taken as the comparison opens: no Set Before any more.
        int setBeforeW = 0;
        int offW = 0;
        int packW = Math.min(font.width(packLabel()) + 16, 160);
        int totalW = backW + 4 + resetW + 4 + packW;
        int x = Math.max(4, (screenW - totalW) / 2);
        int resetX = x + backW + 4;
        int setBeforeX = resetX + resetW + 4;
        int offX = setBeforeX + setBeforeW;
        int packX = setBeforeX;
        return new TopBarRects(x, backW, resetX, resetW, setBeforeX, setBeforeW, offX, offW, packX, packW, 10);
    }

    /** A pack's name as the pack list shows it. */
    private static String packName(String stored) {
        ShaderPackLibrary.Entry entry = stored == null ? null : ShaderPackLibrary.entry(stored);
        return entry != null && entry.displayName() != null ? entry.displayName() : String.valueOf(stored);
    }

    /**
     * Switches the live picture (After) to the next pack that can run, keeping Before: one pack against another,
     * or Radiante PT against a pack. The pack builds in the background as it does from the pack list.
     */
    private static void cyclePack() {
        Minecraft mc = Minecraft.getInstance();
        String current = com.g2806.radiante.client.pipeline.PipelineShaderPack.shaderPackPath();
        java.util.List<ShaderPackLibrary.Entry> usable = new java.util.ArrayList<>();
        for (ShaderPackLibrary.Entry entry : ShaderPackLibrary.scan()) {
            if (entry.problem() == null
                && !com.g2806.radiante.client.pipeline.PipelineShaderPack.shaderPackUnavailable(entry.path())) {
                usable.add(entry);
            }
        }
        if (usable.size() < 2) {
            return;
        }
        int at = 0;
        for (int i = 0; i < usable.size(); i++) {
            if (ShaderPackLibrary.samePack(usable.get(i).path(), current)) {
                at = i;
                break;
            }
        }
        ShaderPackLibrary.Entry next = usable.get((at + 1) % usable.size());
        if (!com.g2806.radiante.client.pipeline.PipelineShaderPack.selectShaderPack(next.path())) {
            return;
        }
        RadianteClient.LOGGER.info("Before / After: After now with shader pack {}", next.path());
        ShaderPackTrial.begin(next.path(), next.displayName());
        BackgroundRebuild.open(mc, mc.gui.screen(), true);
    }

    static void extract(GuiGraphicsExtractor g, Font font, int screenW, int screenH, int mouseX, int mouseY) {
        // The divider slides to where it is sent (50%, the arrow keys, the wheel) instead of jumping there.
        shownFraction += (splitFraction - shownFraction) * 0.35f;
        if (Math.abs(splitFraction - shownFraction) < 0.0005f) {
            shownFraction = splitFraction;
        }
        int splitX = peeking ? screenW : splitPixel(screenW);

        // Left side: the captured Before. The right side stays uncovered, so the live traced world shows as After.
        if (hasBaseline() && splitX > 0) {
            g.enableScissor(0, 0, splitX, screenH);
            VersionCompat.blit(g, textureId, 0, 0, 0.0f, 0.0f, screenW, screenH, texWidth, texHeight, texWidth, texHeight);
            g.disableScissor();
        }

        if (offCapturePending || System.nanoTime() < coverUntil) {
            // The live world is the game without ray tracing for these two seconds: shown on the right, the halves
            // looked swapped (Off on the right, On on the left) until the picture was taken. The screen is covered
            // instead, with the picture from before.
            if (hasBaseline()) {
                VersionCompat.blit(g, textureId, 0, 0, 0.0f, 0.0f, screenW, screenH, texWidth, texHeight, texWidth,
                    texHeight);
            } else {
                g.fill(0, 0, screenW, screenH, 0xFF0E1010);
            }
            Component wait = Component.translatable("options.radiante.compare.capturing_off");
            int w = font.width(wait) + 24;
            g.fill((screenW - w) / 2, screenH / 2 - 12, (screenW + w) / 2, screenH / 2 + 12, 0xD0101413);
            g.outline((screenW - w) / 2, screenH / 2 - 12, w, 24, Theme.ACCENT);
            g.centeredText(font, wait, screenW / 2, screenH / 2 - 4, Theme.ACCENT);
            return;
        }

        boolean nearLine = draggingSplit || Math.abs(mouseX - splitX) <= 14;
        int lineColor = nearLine ? Theme.ACCENT : 0xFFE6FAF5;
        if (!peeking) {
            // A line two pixels wide with a dark edge each side, so it reads against bright sky and dark caves alike.
            g.fill(splitX - 3, 0, splitX - 1, screenH, 0x70000000);
            g.fill(splitX + 1, 0, splitX + 3, screenH, 0x70000000);
            g.fill(splitX - 1, 0, splitX + 1, screenH, lineColor);

            // The grab handle: a knob with rounded corners and an arrow each way, drawn rather than taken from the
            // font, whose arrow characters some resource packs leave out.
            int handleW = 28;
            int handleH = 40;
            int hx = splitX - handleW / 2;
            int hy = screenH / 2 - handleH / 2;
            int fillColor = nearLine ? Theme.PANEL_DARK : Theme.PANEL;
            g.fill(hx + 2, hy, hx + handleW - 2, hy + handleH, fillColor);
            g.fill(hx, hy + 2, hx + handleW, hy + handleH - 2, fillColor);
            g.fill(hx + 2, hy, hx + handleW - 2, hy + 1, lineColor);
            g.fill(hx + 2, hy + handleH - 1, hx + handleW - 2, hy + handleH, lineColor);
            g.fill(hx, hy + 2, hx + 1, hy + handleH - 2, lineColor);
            g.fill(hx + handleW - 1, hy + 2, hx + handleW, hy + handleH - 2, lineColor);
            int cy = hy + handleH / 2;
            for (int i = 0; i < 5; i++) {
                g.fill(hx + 5 + i, cy - i, hx + 6 + i, cy + i + 1, lineColor);
                g.fill(hx + handleW - 6 - i, cy - i, hx + handleW - 5 - i, cy + i + 1, lineColor);
            }
            if (draggingSplit) {
                Component percent = Component.literal(Math.round(splitFraction * 100.0f) + "%");
                int pw = font.width(percent) + 10;
                g.fill(splitX - pw / 2, hy - 18, splitX + pw / 2, hy - 4, 0xC0101413);
                g.centeredText(font, percent, splitX, hy - 15, Theme.ACCENT);
            }
        }

        // What each side is: the settings before and after, or Radiante off and on.
        int margin = Math.max(8, screenW / 32);
        String livePack = com.g2806.radiante.client.pipeline.PipelineShaderPack.shaderPackPath();
        boolean packsDiffer = !beforeIsOff && beforePack != null && !ShaderPackLibrary.samePack(beforePack, livePack);
        Component beforeText = (beforeIsOff ? Component.translatable("options.radiante.compare.off")
            : packsDiffer ? Component.literal(packName(beforePack))
            : Component.translatable("options.radiante.compare.before")).copy().withStyle(ChatFormatting.BOLD);
        Component afterText = (beforeIsOff ? Component.literal(packName(livePack))
            : packsDiffer ? Component.literal(packName(livePack))
            : Component.translatable("options.radiante.compare.after")).copy().withStyle(ChatFormatting.BOLD);
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
        if (peeking) {
            return;
        }

        TopBarRects bar = topBar(font, screenW);
        drawPillButton(g, font, Component.translatable("options.radiante.compare.exit"), bar.backX(), bar.barY(),
            bar.backW(), mouseX, mouseY, false);
        drawPillButton(g, font, Component.literal("50%"), bar.resetX(), bar.barY(), bar.resetW(), mouseX, mouseY,
            false);
        drawPillButton(g, font, packLabel(), bar.packX(), bar.barY(), bar.packW(), mouseX, mouseY, packsDiffer);

        Component hint = Component.translatable("options.radiante.compare.hint");
        int hintW = font.width(hint) + 16;
        if (hintW + beforeW + afterW + margin * 4 < screenW) {
            int hx = (screenW - hintW) / 2;
            g.fill(hx, badgeY + 1, hx + hintW, badgeY + 17, 0xA8101413);
            g.centeredText(font, hint, screenW / 2, badgeY + 5, Theme.TEXT_DIM);
        }
    }

    private static void drawPillButton(GuiGraphicsExtractor g, Font font, Component label, int x, int y, int w,
                                       int mouseX, int mouseY, boolean selected) {
        boolean hover = mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + 18;
        boolean lit = hover || selected;
        g.fill(x, y, x + w, y + 18, lit ? Theme.PANEL_DARK : Theme.PANEL);
        g.outline(x, y, w, 18, lit ? Theme.ACCENT : 0x55FFFFFF);
        // A long pack name is cut to the pill instead of running over the next one.
        Component shown = label;
        if (font.width(label) > w - 8) {
            shown = Component.literal(font.plainSubstrByWidth(label.getString(), w - 14) + "...");
        }
        g.centeredText(font, shown, x + w / 2, y + 5, lit ? Theme.ACCENT : Theme.TEXT);
    }

    static boolean mouseClicked(double mouseX, double mouseY, int button, Font font, int screenW,
                                Runnable onExit, Runnable onRebaseline) {
        if (offCapturePending) {
            return true;
        }
        // The left button's number differs between versions (0 up to 26.2, 1 from 26.3 on): the check took 0, and
        // on 26.3 neither the buttons nor dragging the divider answered a click.
        if (button != InputConstants.MOUSE_BUTTON_LEFT) {
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
            if (mouseX >= bar.packX() && mouseX < bar.packX() + bar.packW()) {
                cyclePack();
                return true;
            }
        }
        draggingSplit = true;
        dragTo((float) (mouseX / Math.max(1, screenW)));
        return true;
    }

    static boolean mouseDragged(double mouseX, int screenW) {
        if (!draggingSplit) {
            return false;
        }
        dragTo((float) (mouseX / Math.max(1, screenW)));
        return true;
    }

    static void mouseReleased() {
        draggingSplit = false;
    }

    /** The mouse wheel moves the divider a step, as the arrow keys do. */
    static void mouseScrolled(double scrollY) {
        setSplitFraction(splitFraction - (float) scrollY * 0.04f);
    }

    static boolean keyPressed(KeyEvent event, Runnable onExit) {
        if (event.key() == InputConstants.KEY_ESCAPE) {
            onExit.run();
            return true;
        }
        if (event.key() == InputConstants.KEY_SPACE) {
            peeking = true;
            return true;
        }
        if (event.isLeft() || event.isRight()) {
            float step = event.hasShiftDown() ? 0.10f : 0.025f;
            setSplitFraction(splitFraction + (event.isLeft() ? -step : step));
            return true;
        }
        if (event.key() == InputConstants.KEY_HOME || event.key() == InputConstants.KEY_END) {
            setSplitFraction(event.key() == InputConstants.KEY_HOME ? 0.0f : 1.0f);
            return true;
        }
        return false;
    }

    static boolean keyReleased(KeyEvent event) {
        if (event.key() == InputConstants.KEY_SPACE) {
            peeking = false;
            return true;
        }
        return false;
    }

    private static int splitPixel(int screenW) {
        return Math.clamp(Math.round(shownFraction * screenW), 0, screenW);
    }
}
