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

    /**
     * Before taken with another shader pack (its label's menu): the pack is put on and built, the picture left to
     * settle and taken, and the pack the live picture had put back.
     */
    private enum PackCapture { NONE, BUILDING, SETTLING, CAPTURING, RETURNING }

    private static PackCapture packCapture = PackCapture.NONE;
    private static String packCaptureTarget;
    private static String packCaptureReturn;
    /** BUILDING: when to give up (nanoTime); SETTLING: when to take the picture. */
    private static long packCaptureAt;
    private static final long PACK_BUILD_TIMEOUT_NANOS = 60_000_000_000L;
    /** The denoiser's history and the eye's adaptation after a pack change, before the picture is taken. */
    private static final long PACK_SETTLE_NANOS = 1_500_000_000L;

    /** The label whose menu is open: 0 none, 1 Before's, 2 After's. */
    private static int menuSide;
    private static java.util.List<Choice> menu = java.util.List.of();
    private static final int MENU_ROW = 14;

    /** One entry of a label's menu: the game without Radiante (pack null), or a shader pack. */
    private record Choice(Component label, String pack) {
    }

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
        menuSide = 0;
        capturedLevel = null;
        releaseTexture();
        finishOffCapture();
        // Left in the middle of a picture with another pack: the live picture's pack goes back on.
        if (packCapture == PackCapture.BUILDING || packCapture == PackCapture.SETTLING
            || packCapture == PackCapture.CAPTURING) {
            returnPack();
        }
        packCapture = PackCapture.NONE;
    }

    /** A picture is being taken (Radiante Off, or another pack): the halves are covered and clicks wait. */
    private static boolean capturing() {
        return offCapturePending || packCapture != PackCapture.NONE;
    }

    /**
     * Takes Before with a shader pack other than the live one, so the halves compare two packs. The same pack as
     * the live picture takes the live picture as it is.
     */
    static void requestPackBaseline(String pack) {
        Minecraft mc = Minecraft.getInstance();
        if (capturing() || mc.level == null || !RadianteRenderer.isRayTracingEnabled()) {
            return;
        }
        String live = com.g2806.radiante.client.pipeline.PipelineShaderPack.shaderPackPath();
        if (ShaderPackLibrary.samePack(pack, live)) {
            requestRebaseline();
            return;
        }
        if (!com.g2806.radiante.client.pipeline.PipelineShaderPack.selectShaderPack(pack)) {
            return;
        }
        RadianteClient.LOGGER.info("Before / After: taking Before with shader pack {}", pack);
        packCaptureTarget = pack;
        packCaptureReturn = live;
        packCapture = PackCapture.BUILDING;
        packCaptureAt = System.nanoTime() + PACK_BUILD_TIMEOUT_NANOS;
        BackgroundRebuild.apply(mc);
    }

    /** Each traced frame: moves a picture with another pack on, step by step. */
    private static void advancePackCapture() {
        if (packCapture == PackCapture.NONE) {
            return;
        }
        Minecraft mc = Minecraft.getInstance();
        long now = System.nanoTime();
        switch (packCapture) {
            case BUILDING, RETURNING -> {
                if (BackgroundRebuild.busy()) {
                    // A pack that cannot compile in the background is built behind the wait screen now: the wait
                    // screen would otherwise open only once the settings closed, and this would wait for ever.
                    BackgroundRebuild.showWaitScreenNow(mc);
                    if (packCapture == PackCapture.BUILDING && now > packCaptureAt) {
                        RadianteClient.LOGGER.warn("Before / After: {} took too long to build", packCaptureTarget);
                        returnPack();
                    }
                    return;
                }
                if (!(mc.gui.screen() instanceof RadianteOptionsScreen)) {
                    // The wait screen is still up.
                    return;
                }
                if (packCapture == PackCapture.BUILDING) {
                    packCapture = PackCapture.SETTLING;
                    packCaptureAt = now + PACK_SETTLE_NANOS;
                } else {
                    packCapture = PackCapture.NONE;
                    coverUntil = now + ON_SETTLE_NANOS;
                }
            }
            case SETTLING -> {
                if (now < packCaptureAt || baselineRequested) {
                    return;
                }
                packCapture = PackCapture.CAPTURING;
                afterBaselineCallback = () -> {
                    beforeIsOff = false;
                    returnPack();
                };
                baselineRequested = true;
                waitFrames = 0;
            }
            default -> {
            }
        }
    }

    /** The live picture's pack back on, built in the background as a change from the pack list is. */
    private static void returnPack() {
        packCapture = PackCapture.RETURNING;
        if (packCaptureReturn != null
            && com.g2806.radiante.client.pipeline.PipelineShaderPack.selectShaderPack(packCaptureReturn)) {
            BackgroundRebuild.apply(Minecraft.getInstance());
        }
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
        advancePackCapture();
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

    /** The two small buttons at the top, back to the settings and the divider to the middle, as icons. */
    private record TopBarRects(int backX, int centerX, int barY) {
    }

    private static final int ICON_BUTTON = 18;

    private static TopBarRects topBar(int screenW) {
        int x = (screenW - (ICON_BUTTON * 2 + 4)) / 2;
        return new TopBarRects(x, x + ICON_BUTTON + 4, 8);
    }

    /** Where the two labels along the bottom are, and whether each fits beside the divider. */
    private record Badges(int beforeX, int beforeW, boolean beforeShown, int afterX, int afterW, boolean afterShown,
                          int y) {
    }

    private static Badges badges(Font font, int screenW, int screenH) {
        int splitX = peeking ? screenW : splitPixel(screenW);
        int margin = Math.max(8, screenW / 32);
        // Text, and the small triangle that says the label opens a menu.
        int beforeW = font.width(beforeLabel()) + 24;
        int afterW = font.width(afterLabel()) + 24;
        int afterX = screenW - margin - afterW;
        return new Badges(margin, beforeW, splitX > margin + beforeW + 6, afterX, afterW,
            screenW - splitX > margin + afterW + 6, screenH - 28);
    }

    private static boolean packsDiffer() {
        String livePack = com.g2806.radiante.client.pipeline.PipelineShaderPack.shaderPackPath();
        return !beforeIsOff && beforePack != null && !ShaderPackLibrary.samePack(beforePack, livePack);
    }

    /** What the left side is: Radiante off, the pack it was taken with, or the settings before. */
    private static Component beforeLabel() {
        return (beforeIsOff ? Component.translatable("options.radiante.compare.off")
            : packsDiffer() ? Component.literal(packName(beforePack))
            : Component.translatable("options.radiante.compare.before")).copy().withStyle(ChatFormatting.BOLD);
    }

    private static Component afterLabel() {
        String livePack = com.g2806.radiante.client.pipeline.PipelineShaderPack.shaderPackPath();
        return (beforeIsOff || packsDiffer() ? Component.literal(packName(livePack))
            : Component.translatable("options.radiante.compare.after")).copy().withStyle(ChatFormatting.BOLD);
    }

    /** The packs that can run, the built-in one among them. */
    private static java.util.List<ShaderPackLibrary.Entry> usablePacks() {
        java.util.List<ShaderPackLibrary.Entry> usable = new java.util.ArrayList<>();
        for (ShaderPackLibrary.Entry entry : ShaderPackLibrary.scan()) {
            if (entry.problem() == null
                && !com.g2806.radiante.client.pipeline.PipelineShaderPack.shaderPackUnavailable(entry.path())) {
                usable.add(entry);
            }
        }
        return usable;
    }

    /** Opens a label's menu: Before chooses Radiante off or a pack to be pictured with, After the live pack. */
    private static void openMenu(int side) {
        java.util.List<Choice> choices = new java.util.ArrayList<>();
        if (side == 1) {
            choices.add(new Choice(Component.translatable("options.radiante.compare.menu.off"), null));
        }
        for (ShaderPackLibrary.Entry entry : usablePacks()) {
            choices.add(new Choice(Component.literal(entry.displayName() != null ? entry.displayName()
                : entry.path()), entry.path()));
        }
        menu = choices;
        menuSide = side;
    }

    /** Whether a menu entry is what its side shows now. */
    private static boolean chosen(Choice choice) {
        if (menuSide == 1) {
            return choice.pack() == null ? beforeIsOff
                : !beforeIsOff && beforePack != null && ShaderPackLibrary.samePack(choice.pack(), beforePack);
        }
        return ShaderPackLibrary.samePack(choice.pack(),
            com.g2806.radiante.client.pipeline.PipelineShaderPack.shaderPackPath());
    }

    private record MenuRect(int x, int y, int w) {
    }

    /** Above its label, as wide as its longest entry. */
    private static MenuRect menuRect(Font font, Badges badges, int screenW) {
        int w = 0;
        for (Choice choice : menu) {
            w = Math.max(w, font.width(choice.label()) + 20);
        }
        w = Math.min(w, Math.max(80, screenW / 2));
        int x = menuSide == 1 ? badges.beforeX() : badges.afterX() + badges.afterW() - w;
        x = Math.clamp(x, 4, Math.max(4, screenW - w - 4));
        int y = Math.max(4, badges.y() - 4 - menu.size() * MENU_ROW);
        return new MenuRect(x, y, w);
    }

    private static void choose(Choice choice) {
        if (menuSide == 1) {
            if (choice.pack() == null) {
                requestOffBaseline();
            } else {
                requestPackBaseline(choice.pack());
            }
        } else if (!chosen(choice)) {
            selectAfterPack(choice.pack());
        }
    }

    /** A pack's name as the pack list shows it. */
    private static String packName(String stored) {
        ShaderPackLibrary.Entry entry = stored == null ? null : ShaderPackLibrary.entry(stored);
        return entry != null && entry.displayName() != null ? entry.displayName() : String.valueOf(stored);
    }

    /**
     * Switches the live picture (After) to a pack, keeping Before: one pack against another, or Radiante off against
     * a pack. The pack builds in the background as it does from the pack list, and stays chosen.
     */
    private static void selectAfterPack(String path) {
        Minecraft mc = Minecraft.getInstance();
        if (!com.g2806.radiante.client.pipeline.PipelineShaderPack.selectShaderPack(path)) {
            return;
        }
        RadianteClient.LOGGER.info("Before / After: After now with shader pack {}", path);
        ShaderPackTrial.begin(path, packName(path));
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

        if (capturing() || System.nanoTime() < coverUntil) {
            // The live world is not yet what the right side shows (the game without ray tracing, or the pack Before
            // is being taken with): shown, the halves looked swapped until the picture was taken. The screen is
            // covered instead, with the picture from before.
            if (hasBaseline()) {
                VersionCompat.blit(g, textureId, 0, 0, 0.0f, 0.0f, screenW, screenH, texWidth, texHeight, texWidth,
                    texHeight);
            } else {
                g.fill(0, 0, screenW, screenH, 0xFF0E1010);
            }
            Component wait = packCapture != PackCapture.NONE && packCapture != PackCapture.RETURNING
                ? Component.translatable("options.radiante.compare.capturing_pack", packName(packCaptureTarget))
                : Component.translatable("options.radiante.compare.capturing_off");
            int w = font.width(wait) + 24;
            g.fill((screenW - w) / 2, screenH / 2 - 12, (screenW + w) / 2, screenH / 2 + 12, 0xD0101413);
            g.outline((screenW - w) / 2, screenH / 2 - 12, w, 24, Theme.ACCENT);
            g.centeredText(font, wait, screenW / 2, screenH / 2 - 4, Theme.ACCENT);
            return;
        }

        boolean nearLine = draggingSplit || Math.abs(mouseX - splitX) <= 14;
        int lineColor = nearLine ? Theme.ACCENT : 0xFFE6FAF5;
        if (!peeking) {
            // A thin line with a dark edge each side, so it reads against bright sky and dark caves alike, and a
            // small grip in its middle: the arrows and the knob round them hid the middle of the view.
            g.fill(splitX - 2, 0, splitX - 1, screenH, 0x60000000);
            g.fill(splitX + 1, 0, splitX + 2, screenH, 0x60000000);
            g.fill(splitX - 1, 0, splitX + 1, screenH, lineColor);
            int gripH = 24;
            int gy = screenH / 2 - gripH / 2;
            g.fill(splitX - 4, gy, splitX + 4, gy + gripH, 0x90000000);
            g.fill(splitX - 3, gy + 1, splitX + 3, gy + gripH - 1, lineColor);
            if (draggingSplit) {
                Component percent = Component.literal(Math.round(splitFraction * 100.0f) + "%");
                int pw = font.width(percent) + 10;
                g.fill(splitX - pw / 2, gy - 18, splitX + pw / 2, gy - 4, 0xC0101413);
                g.centeredText(font, percent, splitX, gy - 15, Theme.ACCENT);
            }
        }

        // What each side is, each a label that opens a menu to choose it.
        Badges badges = badges(font, screenW, screenH);
        boolean menuOpen = menuSide != 0;
        if (badges.beforeShown()) {
            drawBadge(g, font, beforeLabel(), badges.beforeX(), badges.y(), badges.beforeW(), mouseX, mouseY,
                menuSide == 1, false);
        }
        if (badges.afterShown()) {
            drawBadge(g, font, afterLabel(), badges.afterX(), badges.y(), badges.afterW(), mouseX, mouseY,
                menuSide == 2, true);
        }
        if (peeking) {
            return;
        }

        TopBarRects bar = topBar(screenW);
        boolean overBack = drawIconButton(g, bar.backX(), bar.barY(), mouseX, mouseY, false);
        drawBackIcon(g, bar.backX() + ICON_BUTTON / 2, bar.barY() + ICON_BUTTON / 2, overBack);
        boolean overCenter = drawIconButton(g, bar.centerX(), bar.barY(), mouseX, mouseY, false);
        drawCenterIcon(g, bar.centerX() + ICON_BUTTON / 2, bar.barY() + ICON_BUTTON / 2, overCenter);

        Component hint = Component.translatable("options.radiante.compare.hint");
        int hintW = font.width(hint) + 16;
        if (!menuOpen && hintW + badges.beforeW() + badges.afterW() + Math.max(8, screenW / 32) * 4 < screenW) {
            int hx = (screenW - hintW) / 2;
            g.fill(hx, badges.y() + 1, hx + hintW, badges.y() + 17, 0xA8101413);
            g.centeredText(font, hint, screenW / 2, badges.y() + 5, Theme.TEXT_DIM);
        }

        if (menuOpen) {
            MenuRect rect = menuRect(font, badges, screenW);
            g.fill(rect.x() - 1, rect.y() - 1, rect.x() + rect.w() + 1, rect.y() + menu.size() * MENU_ROW + 1,
                Theme.ACCENT);
            for (int i = 0; i < menu.size(); i++) {
                Choice choice = menu.get(i);
                int ry = rect.y() + i * MENU_ROW;
                boolean hover = mouseX >= rect.x() && mouseX < rect.x() + rect.w() && mouseY >= ry
                    && mouseY < ry + MENU_ROW;
                g.fill(rect.x(), ry, rect.x() + rect.w(), ry + MENU_ROW, hover ? Theme.PANEL_DARK : 0xF0101413);
                boolean current = chosen(choice);
                String text = font.plainSubstrByWidth(choice.label().getString(), rect.w() - 16);
                if (current) {
                    g.fill(rect.x() + 4, ry + 5, rect.x() + 8, ry + 9, Theme.ACCENT);
                }
                g.text(font, Component.literal(text), rect.x() + 12, ry + 3,
                    current || hover ? Theme.ACCENT : Theme.TEXT);
            }
            return;
        }

        // What the icons and labels do, in words, as the mouse rests on them.
        Component tip = null;
        if (overBack) {
            tip = Component.translatable("options.radiante.compare.exit.tooltip");
        } else if (overCenter) {
            tip = Component.translatable("options.radiante.compare.center.tooltip");
        } else if (badges.beforeShown() && inside(mouseX, mouseY, badges.beforeX(), badges.y(), badges.beforeW())) {
            tip = Component.translatable("options.radiante.compare.before.tooltip");
        } else if (badges.afterShown() && inside(mouseX, mouseY, badges.afterX(), badges.y(), badges.afterW())) {
            tip = Component.translatable("options.radiante.compare.after.tooltip");
        }
        if (tip != null) {
            g.setTooltipForNextFrame(font.split(tip, 200), mouseX, mouseY);
        }
    }

    private static boolean inside(int mouseX, int mouseY, int x, int y, int w) {
        return mouseX >= x && mouseX < x + w && mouseY >= y && mouseY < y + 18;
    }

    /** A label along the bottom: its text, and a small triangle pointing up to the menu it opens. */
    private static void drawBadge(GuiGraphicsExtractor g, Font font, Component text, int x, int y, int w, int mouseX,
                                  int mouseY, boolean open, boolean live) {
        boolean lit = open || inside(mouseX, mouseY, x, y, w);
        int color = live || lit ? Theme.ACCENT : Theme.TEXT;
        g.fill(x, y, x + w, y + 18, lit ? Theme.PANEL_DARK : Theme.PANEL);
        g.outline(x, y, w, 18, live || lit ? Theme.ACCENT : 0x60FFFFFF);
        g.text(font, text, x + 7, y + 5, color);
        int tx = x + w - 11;
        for (int i = 0; i < 3; i++) {
            g.fill(tx + i, y + 8 + i, tx + 6 - i, y + 9 + i, color);
        }
    }

    /** An icon's square button; true while the mouse is over it. */
    private static boolean drawIconButton(GuiGraphicsExtractor g, int x, int y, int mouseX, int mouseY,
                                          boolean selected) {
        boolean hover = inside(mouseX, mouseY, x, y, ICON_BUTTON);
        boolean lit = hover || selected;
        g.fill(x, y, x + ICON_BUTTON, y + ICON_BUTTON, lit ? Theme.PANEL_DARK : Theme.PANEL);
        g.outline(x, y, ICON_BUTTON, ICON_BUTTON, lit ? Theme.ACCENT : 0x55FFFFFF);
        return hover;
    }

    /** A chevron pointing left: back to the settings. */
    private static void drawBackIcon(GuiGraphicsExtractor g, int cx, int cy, boolean lit) {
        int c = lit ? Theme.ACCENT : Theme.TEXT;
        for (int i = 0; i < 4; i++) {
            g.fill(cx - 2 + i, cy - i - 1, cx + i, cy - i, c);
            g.fill(cx - 2 + i, cy + i, cx + i, cy + i + 1, c);
        }
    }

    /** A frame split down its middle: the divider to the middle. */
    private static void drawCenterIcon(GuiGraphicsExtractor g, int cx, int cy, boolean lit) {
        int c = lit ? Theme.ACCENT : Theme.TEXT;
        g.outline(cx - 5, cy - 4, 10, 8, c);
        g.fill(cx - 1, cy - 6, cx + 1, cy + 6, c);
    }

    static boolean mouseClicked(double mouseX, double mouseY, int button, Font font, int screenW, int screenH,
                                Runnable onExit) {
        if (capturing()) {
            return true;
        }
        // The left button's number differs between versions (0 up to 26.2, 1 from 26.3 on): the check took 0, and
        // on 26.3 neither the buttons nor dragging the divider answered a click.
        if (button != InputConstants.MOUSE_BUTTON_LEFT) {
            return false;
        }
        int x = (int) mouseX;
        int y = (int) mouseY;
        Badges badges = badges(font, screenW, screenH);
        if (menuSide != 0) {
            // A click picks an entry, or closes the menu anywhere else.
            MenuRect rect = menuRect(font, badges, screenW);
            int row = (y - rect.y()) / MENU_ROW;
            if (x >= rect.x() && x < rect.x() + rect.w() && y >= rect.y() && row >= 0 && row < menu.size()) {
                choose(menu.get(row));
            }
            menuSide = 0;
            return true;
        }
        if (badges.beforeShown() && inside(x, y, badges.beforeX(), badges.y(), badges.beforeW())) {
            openMenu(1);
            return true;
        }
        if (badges.afterShown() && inside(x, y, badges.afterX(), badges.y(), badges.afterW())) {
            openMenu(2);
            return true;
        }
        TopBarRects bar = topBar(screenW);
        if (inside(x, y, bar.backX(), bar.barY(), ICON_BUTTON)) {
            onExit.run();
            return true;
        }
        if (inside(x, y, bar.centerX(), bar.barY(), ICON_BUTTON)) {
            setSplitFraction(0.5f);
            return true;
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
            if (menuSide != 0) {
                menuSide = 0;
            } else if (!capturing()) {
                onExit.run();
            }
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
