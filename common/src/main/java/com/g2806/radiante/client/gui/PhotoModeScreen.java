package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.photo.PhotoMode;
import com.g2806.radiante.compat.VersionCompat;
import com.mojang.blaze3d.platform.InputConstants;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.function.Consumer;
import java.util.function.Supplier;
import net.minecraft.ChatFormatting;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonEvent;
import net.minecraft.network.chat.Component;

/**
 * Interactive Photo Mode overlay screen: leaves the live reference path-traced viewport unobstructed on the left,
 * lets the player fly with WASD / Space / Shift, drag to aim, click in the world to pull focus, and adjust the
 * physical thin-lens camera, FOV, roll, exposure and PNG / HDR OpenEXR capture in a compact panel on the right.
 */
public final class PhotoModeScreen extends VersionCompat.DrawnScreen {

    public static final Component TITLE = Component.translatable("gui.radiante.photo.title");

    private static final int ACCENT = Theme.ACCENT;
    private static final int TEXT = Theme.TEXT;
    private static final int TEXT_DIM = Theme.TEXT_DIM;
    private static final int PANEL = Theme.PANEL;
    private static final int PANEL_DARK = Theme.PANEL_DARK;
    private static final int ROW_HOVER = Theme.ROW_HOVER;
    private static final int DISABLED = Theme.DISABLED;
    private static final int BAD = Theme.BAD;
    private static final int PAD = 6;
    private static final int BUTTON_H = 15;
    private static final int ROW_H = 17;

    private final Screen parent;
    private boolean hudHidden;

    private int panelX;
    private int panelY;
    private int panelW;
    private int panelH;
    // The Cinematic panel, on the left: framing and, in singleplayer, the time of day and the weather.
    private int cinemaX;
    private int cinemaY;
    private int cinemaW;
    private int cinemaH;

    private final List<SliderRow> sliders = new ArrayList<>();
    private final List<ButtonBox> buttons = new ArrayList<>();
    private SliderRow activeSlider;

    private boolean viewportDragging;
    private boolean viewportMoved;
    private double pressX;
    private double pressY;

    private record SliderRow(String labelKey, String tooltipKey, int x, int y, int w,
                             Supplier<Float> normGetter, Consumer<Float> normSetter,
                             Supplier<Component> valueText, Supplier<Boolean> enabled) {
    }

    private record ButtonBox(Supplier<Component> label, String tooltipKey, int x, int y, int w, int h,
                             Runnable action, Supplier<Boolean> enabled, boolean accent) {
    }

    public PhotoModeScreen(Screen parent) {
        super(TITLE);
        this.parent = parent;
    }

    /** Opens Photo Mode when ray tracing is active in a world; returns false when unavailable. */
    public static boolean open(Minecraft minecraft, Screen parent) {
        if (!PhotoMode.enter(minecraft)) {
            return false;
        }
        minecraft.gui.setScreen(new PhotoModeScreen(parent));
        return true;
    }

    @Override
    public boolean isPauseScreen() {
        // Except for the moment the sky and the rain take to follow a change of time or weather (PhotoCinema).
        return !PhotoMode.isFollowingSky();
    }

    @Override
    protected boolean extractOwnBackground(GuiGraphicsExtractor graphics) {
        // Draw no blur or dirt backdrop so the live path-traced picture fills the viewport.
        return true;
    }

    @Override
    protected void init() {
        this.sliders.clear();
        this.buttons.clear();
        this.activeSlider = null;

        this.panelW = Math.min(212, Math.max(180, this.width / 4));
        int margin = 6;
        this.panelX = this.width - this.panelW - margin;
        this.panelY = margin;

        int innerX = this.panelX + PAD;
        int innerW = this.panelW - PAD * 2;
        int y = this.panelY + 38;

        // Target samples & Blades cycle row
        int halfW = (innerW - 4) / 2;
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.target_samples", PhotoMode.targetSamples()),
            "gui.radiante.photo.target_samples.tooltip", innerX, y, halfW, BUTTON_H,
            PhotoMode::cycleTargetSamples, () -> true, false));
        this.buttons.add(new ButtonBox(
            () -> PhotoMode.blades() < 3
                ? Component.translatable("gui.radiante.photo.blades.round")
                : Component.translatable("gui.radiante.photo.blades.count", PhotoMode.blades()),
            "gui.radiante.photo.blades.tooltip", innerX + halfW + 4, y, innerW - halfW - 4, BUTTON_H,
            PhotoMode::cycleBlades, () -> true, false));
        y += BUTTON_H + 4;

        this.buttons.add(new ButtonBox(
            () -> Component.translatable(PhotoMode.denoise()
                ? "gui.radiante.photo.denoiser.on" : "gui.radiante.photo.denoiser.off"),
            "gui.radiante.photo.denoiser.tooltip", innerX, y, innerW, BUTTON_H,
            () -> PhotoMode.setDenoise(!PhotoMode.denoise()), () -> true, false));
        y += BUTTON_H + 4;

        // Physical thin-lens & camera sliders
        addSlider("gui.radiante.photo.aperture", "gui.radiante.photo.aperture.tooltip", innerX, y, innerW,
            () -> PhotoMode.apertureRadius() / 0.40f,
            t -> PhotoMode.setApertureRadius(t * 0.40f),
            () -> PhotoMode.apertureRadius() < 1.0e-4f
                ? Component.translatable("gui.radiante.photo.aperture.pinhole")
                : Component.literal(String.format(Locale.ROOT, "%.3f", PhotoMode.apertureRadius())),
            () -> true);
        y += ROW_H;

        addSlider("gui.radiante.photo.focus_distance", "gui.radiante.photo.focus_distance.tooltip", innerX, y, innerW,
            () -> focusToNorm(PhotoMode.focusDistance()),
            t -> PhotoMode.setFocusDistance(normToFocus(t)),
            () -> Component.translatable("gui.radiante.photo.blocks_value",
                String.format(Locale.ROOT, "%.2f", PhotoMode.focusDistance())),
            () -> PhotoMode.apertureRadius() >= 1.0e-4f);
        y += ROW_H;

        addSlider("gui.radiante.photo.blade_angle", "gui.radiante.photo.blade_angle.tooltip", innerX, y, innerW,
            () -> PhotoMode.bladeRotationDegrees() / 180.0f,
            t -> PhotoMode.setBladeRotationDegrees(t * 180.0f),
            () -> Component.literal(Math.round(PhotoMode.bladeRotationDegrees()) + "°"),
            () -> PhotoMode.blades() >= 3 && PhotoMode.apertureRadius() >= 1.0e-4f);
        y += ROW_H;

        addSlider("gui.radiante.photo.fov", "gui.radiante.photo.fov.tooltip", innerX, y, innerW,
            () -> (PhotoMode.fovDegrees() - 15.0f) / (120.0f - 15.0f),
            t -> PhotoMode.setFovDegrees(15.0f + t * (120.0f - 15.0f)),
            () -> Component.literal(Math.round(PhotoMode.fovDegrees()) + "° · "
                + com.g2806.radiante.client.photo.PhotoCinema.focalLabel()),
            () -> true);
        y += ROW_H;

        addSlider("gui.radiante.photo.roll", "gui.radiante.photo.roll.tooltip", innerX, y, innerW,
            () -> (PhotoMode.rollDegrees() + 45.0f) / 90.0f,
            t -> PhotoMode.setRollDegrees(-45.0f + t * 90.0f),
            () -> Component.literal(String.format(Locale.ROOT, "%+.1f°", PhotoMode.rollDegrees())),
            () -> true);
        y += ROW_H;

        addSlider("gui.radiante.photo.exposure", "gui.radiante.photo.exposure.tooltip", innerX, y, innerW,
            () -> (PhotoMode.exposureBias() + 4.0f) / 8.0f,
            t -> PhotoMode.setExposureBias(-4.0f + t * 8.0f),
            () -> Component.literal(String.format(Locale.ROOT, "%+.2f EV", PhotoMode.exposureBias())),
            () -> true);
        y += ROW_H + 3;

        // Hide Hands toggle + Folder button
        this.buttons.add(new ButtonBox(
            () -> Component.translatable(PhotoMode.hideHands()
                ? "gui.radiante.photo.hands.hidden" : "gui.radiante.photo.hands.shown"),
            "gui.radiante.photo.hands.tooltip", innerX, y, halfW, BUTTON_H,
            () -> PhotoMode.setHideHands(!PhotoMode.hideHands()), () -> true, false));
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.open_folder"),
            "gui.radiante.photo.open_folder.tooltip", innerX + halfW + 4, y, innerW - halfW - 4, BUTTON_H,
            () -> {
                if (this.minecraft != null) {
                    Path dir = this.minecraft.gameDirectory.toPath().resolve("screenshots");
                    VersionCompat.openFolder(dir);
                }
            }, () -> true, false));
        y += BUTTON_H + 4;

        // Save PNG / Save EXR
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.save_png"),
            "gui.radiante.photo.save_png.tooltip", innerX, y, halfW, BUTTON_H,
            () -> {
                if (this.minecraft != null) {
                    PhotoMode.requestSave(this.minecraft, true, false, true);
                }
            }, () -> !PhotoMode.isCapturePending(), true));
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.save_exr"),
            "gui.radiante.photo.save_exr.tooltip", innerX + halfW + 4, y, innerW - halfW - 4, BUTTON_H,
            () -> {
                if (this.minecraft != null) {
                    PhotoMode.requestSave(this.minecraft, false, true, true);
                }
            }, () -> !PhotoMode.isCapturePending(), true));
        y += BUTTON_H + 3;

        // Save PNG + EXR
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.save_both"),
            "gui.radiante.photo.save_both.tooltip", innerX, y, innerW, BUTTON_H,
            () -> {
                if (this.minecraft != null) {
                    PhotoMode.requestSave(this.minecraft, true, true, true);
                }
            }, () -> !PhotoMode.isCapturePending(), true));
        y += BUTTON_H + 4;

        // Reset / Hide UI / Close
        int thirdW = (innerW - 8) / 3;
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.reset"),
            "gui.radiante.photo.reset.tooltip", innerX, y, thirdW, BUTTON_H,
            () -> PhotoMode.resetParameters(this.minecraft), () -> true, false));
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.hide_ui"),
            "gui.radiante.photo.hide_ui.tooltip", innerX + thirdW + 4, y, thirdW, BUTTON_H,
            () -> this.hudHidden = true, () -> true, false));
        int closeX = innerX + (thirdW + 4) * 2;
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.done"),
            null, closeX, y, innerX + innerW - closeX, BUTTON_H,
            this::onClose, () -> true, false));
        y += BUTTON_H + PAD;

        this.panelH = y - this.panelY;
        initCinema(margin);
    }

    private void initCinema(int margin) {
        this.cinemaW = this.panelW;
        this.cinemaX = margin;
        this.cinemaY = margin;
        int innerX = this.cinemaX + PAD;
        int innerW = this.cinemaW - PAD * 2;
        int halfW = (innerW - 4) / 2;
        int y = this.cinemaY + 18;
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.frame",
                Component.translatable(com.g2806.radiante.client.photo.PhotoCinema.frame().label())),
            "gui.radiante.photo.frame.tooltip", innerX, y, innerW, BUTTON_H,
            com.g2806.radiante.client.photo.PhotoCinema::cycleFrame, () -> true, false));
        y += BUTTON_H + 4;
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.guides." + com.g2806.radiante.client.photo.PhotoCinema
                .guides().name().toLowerCase(java.util.Locale.ROOT)),
            "gui.radiante.photo.guides.tooltip", innerX, y, halfW, BUTTON_H,
            com.g2806.radiante.client.photo.PhotoCinema::cycleGuides, () -> true, false));
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.lens",
                com.g2806.radiante.client.photo.PhotoCinema.focalLabel()),
            "gui.radiante.photo.lens.tooltip", innerX + halfW + 4, y, innerW - halfW - 4, BUTTON_H,
            com.g2806.radiante.client.photo.PhotoCinema::nextLens, () -> true, false));
        y += BUTTON_H + 4;
        // Ready-made scenes (time and weather) and camera looks (lens, depth of field, exposure).
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.scene", com.g2806.radiante.client.photo.PhotoCinema.scene() == null
                ? Component.translatable("gui.radiante.photo.preset.pick")
                : Component.translatable(com.g2806.radiante.client.photo.PhotoCinema.scene().key())),
            "gui.radiante.photo.scene.tooltip", innerX, y, halfW, BUTTON_H,
            () -> com.g2806.radiante.client.photo.PhotoCinema.nextScene(this.minecraft),
            () -> this.minecraft != null && com.g2806.radiante.client.photo.PhotoCinema.ownsWorld(this.minecraft), false));
        this.buttons.add(new ButtonBox(
            () -> Component.translatable("gui.radiante.photo.look", com.g2806.radiante.client.photo.PhotoCinema.look() == null
                ? Component.translatable("gui.radiante.photo.preset.pick")
                : Component.translatable(com.g2806.radiante.client.photo.PhotoCinema.look().key())),
            "gui.radiante.photo.look.tooltip", innerX + halfW + 4, y, innerW - halfW - 4, BUTTON_H,
            com.g2806.radiante.client.photo.PhotoCinema::nextLook, () -> true, false));
        y += BUTTON_H + 4;
        addSlider("gui.radiante.photo.time", "gui.radiante.photo.time.tooltip", innerX, y, innerW,
            () -> Math.max(0L, com.g2806.radiante.client.photo.PhotoCinema.dayTime(this.minecraft)) / 24000.0f,
            t -> {
                // Only a change of a quarter of an hour or more: dragging sent a command a frame otherwise.
                long ticks = Math.round(t * 24000.0) / 250L * 250L;
                if (Math.abs(ticks - com.g2806.radiante.client.photo.PhotoCinema.dayTime(this.minecraft)) >= 250L) {
                    com.g2806.radiante.client.photo.PhotoCinema.setDayTime(this.minecraft, ticks);
                }
            },
            () -> Component.literal(com.g2806.radiante.client.photo.PhotoCinema.clock(
                Math.max(0L, com.g2806.radiante.client.photo.PhotoCinema.dayTime(this.minecraft)))),
            () -> this.minecraft != null && com.g2806.radiante.client.photo.PhotoCinema.ownsWorld(this.minecraft));
        y += ROW_H + 2;
        this.buttons.add(new ButtonBox(
            () -> Component.translatable(com.g2806.radiante.client.photo.PhotoCinema.weatherKey(this.minecraft)),
            "gui.radiante.photo.weather.tooltip", innerX, y, innerW, BUTTON_H,
            () -> com.g2806.radiante.client.photo.PhotoCinema.cycleWeather(this.minecraft),
            () -> this.minecraft != null && com.g2806.radiante.client.photo.PhotoCinema.ownsWorld(this.minecraft),
            false));
        y += BUTTON_H + PAD;
        this.cinemaH = y - this.cinemaY;
    }

    /** The part of the screen inside the chosen frame (Photo Mode's Frame), as x, y, width, height. */
    private int[] frameRect() {
        float aspect = com.g2806.radiante.client.photo.PhotoCinema.frame().aspect();
        float screen = (float) this.width / Math.max(1, this.height);
        if (!(aspect > 0.0f) || Math.abs(aspect - screen) < 0.01f) {
            return new int[] {0, 0, this.width, this.height};
        }
        if (aspect > screen) {
            int h = Math.round(this.width / aspect);
            return new int[] {0, (this.height - h) / 2, this.width, h};
        }
        int w = Math.round(this.height * aspect);
        return new int[] {(this.width - w) / 2, 0, w, this.height};
    }

    /** The frame's bars, as the saved photo is cut, and the composition guides inside it. */
    private void drawFrame(GuiGraphicsExtractor g, boolean guides) {
        int[] r = frameRect();
        int bar = 0xF0000000;
        if (r[1] > 0) {
            g.fill(0, 0, this.width, r[1], bar);
            g.fill(0, r[1] + r[3], this.width, this.height, bar);
        }
        if (r[0] > 0) {
            g.fill(0, 0, r[0], this.height, bar);
            g.fill(r[0] + r[2], 0, this.width, this.height, bar);
        }
        if (!guides) {
            return;
        }
        int line = 0x70FFFFFF;
        int x = r[0];
        int y = r[1];
        int w = r[2];
        int h = r[3];
        switch (com.g2806.radiante.client.photo.PhotoCinema.guides()) {
            case THIRDS -> {
                g.fill(x + w / 3, y, x + w / 3 + 1, y + h, line);
                g.fill(x + w * 2 / 3, y, x + w * 2 / 3 + 1, y + h, line);
                g.fill(x, y + h / 3, x + w, y + h / 3 + 1, line);
                g.fill(x, y + h * 2 / 3, x + w, y + h * 2 / 3 + 1, line);
            }
            case GOLDEN -> {
                float phi = 0.382f;
                g.fill(x + Math.round(w * phi), y, x + Math.round(w * phi) + 1, y + h, line);
                g.fill(x + Math.round(w * (1 - phi)), y, x + Math.round(w * (1 - phi)) + 1, y + h, line);
                g.fill(x, y + Math.round(h * phi), x + w, y + Math.round(h * phi) + 1, line);
                g.fill(x, y + Math.round(h * (1 - phi)), x + w, y + Math.round(h * (1 - phi)) + 1, line);
            }
            case CENTER -> {
                g.fill(x + w / 2 - 8, y + h / 2, x + w / 2 + 9, y + h / 2 + 1, line);
                g.fill(x + w / 2, y + h / 2 - 8, x + w / 2 + 1, y + h / 2 + 9, line);
                g.outline(x + w / 10, y + h / 10, w * 8 / 10, h * 8 / 10, 0x40FFFFFF);
            }
            case DIAGONALS -> {
                int steps = Math.max(w, h);
                for (int i = 0; i < steps; i += 2) {
                    int px = x + i * w / steps;
                    int py = y + i * h / steps;
                    g.fill(px, py, px + 1, py + 1, line);
                    g.fill(px, y + h - 1 - (py - y), px + 1, y + h - (py - y), line);
                }
            }
            default -> {
            }
        }
    }

    private void addSlider(String labelKey, String tooltipKey, int x, int y, int w,
        Supplier<Float> normGetter, Consumer<Float> normSetter, Supplier<Component> valueText,
        Supplier<Boolean> enabled) {
        this.sliders.add(new SliderRow(labelKey, tooltipKey, x, y, w, normGetter, normSetter, valueText, enabled));
    }

    private static float focusToNorm(float dist) {
        float clamped = Math.clamp(dist, 0.25f, 128.0f);
        return (float) Math.sqrt((clamped - 0.25f) / (128.0f - 0.25f));
    }

    private static float normToFocus(float norm) {
        float t = Math.clamp(norm, 0.0f, 1.0f);
        return 0.25f + t * t * (128.0f - 0.25f);
    }

    public boolean allowsKeyboardMove() {
        return this.activeSlider == null;
    }

    @Override
    public void tick() {
        super.tick();
        PhotoMode.pollStatus();
    }

    @Override
    protected void extractContents(GuiGraphicsExtractor g, int mouseX, int mouseY) {
        PhotoMode.pollStatus();
        drawFrame(g, !this.hudHidden);
        if (this.hudHidden) {
            if (PhotoMode.isCapturePending()) {
                drawProgressPill(g);
            }
            return;
        }

        g.fill(this.panelX, this.panelY, this.panelX + this.panelW, this.panelY + this.panelH, PANEL);
        g.outline(this.panelX, this.panelY, this.panelW, this.panelH, 0x40FFFFFF);
        g.fill(this.cinemaX, this.cinemaY, this.cinemaX + this.cinemaW, this.cinemaY + this.cinemaH, PANEL);
        g.outline(this.cinemaX, this.cinemaY, this.cinemaW, this.cinemaH, 0x40FFFFFF);
        g.text(this.font, Component.translatable("gui.radiante.photo.cinematic").withStyle(ChatFormatting.BOLD),
            this.cinemaX + PAD, this.cinemaY + 5, TEXT);

        int innerX = this.panelX + PAD;
        int innerW = this.panelW - PAD * 2;
        int headerY = this.panelY + 5;
        g.text(this.font, TITLE.copy().withStyle(ChatFormatting.BOLD), innerX, headerY, TEXT);

        int current = PhotoMode.sampleCount();
        int target = Math.max(1, PhotoMode.targetSamples());
        int shownSamples = Math.min(current, target);
        Component sampleLabel = Component.translatable("gui.radiante.photo.samples", shownSamples, target);
        g.text(this.font, sampleLabel, innerX, headerY + 11, current >= target ? ACCENT : TEXT_DIM);

        // Progress bar
        int barY = headerY + 22;
        g.fill(innerX, barY, innerX + innerW, barY + 3, PANEL_DARK);
        int fillW = Math.round(innerW * Math.clamp((float) shownSamples / (float) target, 0.0f, 1.0f));
        if (fillW > 0) {
            g.fill(innerX, barY, innerX + fillW, barY + 3, ACCENT);
        }

        Component activeTip = null;

        // Sliders
        for (SliderRow slider : this.sliders) {
            boolean enabled = slider.enabled().get();
            boolean hover = inside(mouseX, mouseY, slider.x(), slider.y(), slider.w(), ROW_H - 1);
            if (hover) {
                g.fill(slider.x() - 2, slider.y(), slider.x() + slider.w() + 2, slider.y() + ROW_H - 1, ROW_HOVER);
                if (slider.tooltipKey() != null) {
                    activeTip = Component.translatable(slider.tooltipKey());
                }
            }
            Component label = Component.translatable(slider.labelKey());
            Component value = slider.valueText().get();
            g.text(this.font, label, slider.x(), slider.y() + 1, enabled ? TEXT : DISABLED);
            g.text(this.font, value, slider.x() + slider.w() - this.font.width(value), slider.y() + 1,
                enabled ? (hover ? ACCENT : TEXT_DIM) : DISABLED);

            int trackY = slider.y() + 11;
            g.fill(slider.x(), trackY, slider.x() + slider.w(), trackY + 2, PANEL_DARK);
            float norm = Math.clamp(slider.normGetter().get(), 0.0f, 1.0f);
            int filled = Math.round(slider.w() * norm);
            if (filled > 0) {
                g.fill(slider.x(), trackY, slider.x() + filled, trackY + 2, enabled ? ACCENT : DISABLED);
            }
        }

        // Buttons
        for (ButtonBox button : this.buttons) {
            boolean enabled = button.enabled().get();
            boolean hover = enabled && inside(mouseX, mouseY, button.x(), button.y(), button.w(), button.h());
            g.fill(button.x(), button.y(), button.x() + button.w(), button.y() + button.h(),
                hover ? PANEL_DARK : 0x90000000);
            if (hover) {
                g.fill(button.x(), button.y() + button.h() - 1, button.x() + button.w(), button.y() + button.h(), ACCENT);
            }
            int color = !enabled ? DISABLED : (hover || button.accent()) ? ACCENT : TEXT;
            g.centeredText(this.font, button.label().get(), button.x() + button.w() / 2, button.y() + 4, color);
            if (button.tooltipKey() != null && inside(mouseX, mouseY, button.x(), button.y(), button.w(), button.h())) {
                activeTip = Component.translatable(button.tooltipKey());
            }
        }

        // Bottom viewport hint & capture banner
        int maxHintRoom = this.panelX - 16;
        if (maxHintRoom > 80) {
            String rawHint = Component.translatable("gui.radiante.photo.controls_hint").getString();
            if (this.font.width(rawHint) > maxHintRoom - 12) {
                rawHint = this.font.plainSubstrByWidth(rawHint, Math.max(0, maxHintRoom - 18)) + "…";
            }
            int hintW = this.font.width(rawHint) + 12;
            int hintY = this.height - 18;
            g.fill(6, hintY, 6 + hintW, hintY + 13, PANEL);
            g.text(this.font, Component.literal(rawHint), 12, hintY + 3, TEXT_DIM);
        }

        if (PhotoMode.isCapturePending()) {
            drawBanner(g, Component.translatable("gui.radiante.photo.saving_wait", shownSamples, target), ACCENT);
        } else {
            String banner = PhotoMode.activeBannerText();
            if (!banner.isEmpty()) {
                boolean failed = PhotoMode.didLastCaptureFail();
                Component msg = failed
                    ? Component.translatable("gui.radiante.photo.save_failed", banner)
                    : Component.translatable("gui.radiante.photo.saved", banner);
                drawBanner(g, msg, failed ? BAD : ACCENT);
            }
        }

        if (activeTip != null) {
            g.setTooltipForNextFrame(this.font.split(activeTip, 200), mouseX, mouseY);
        }
    }

    private void drawProgressPill(GuiGraphicsExtractor g) {
        int current = Math.min(PhotoMode.sampleCount(), PhotoMode.targetSamples());
        Component text = Component.translatable("gui.radiante.photo.saving_wait", current, PhotoMode.targetSamples());
        drawBanner(g, text, ACCENT);
    }

    private void drawBanner(GuiGraphicsExtractor g, Component text, int color) {
        int w = this.font.width(text) + 16;
        int x = 8;
        int y = 8;
        g.fill(x, y, x + w, y + 16, PANEL_DARK);
        g.outline(x, y, w, 16, color);
        g.text(this.font, text, x + 8, y + 4, color);
    }

    private boolean insidePanel(double x, double y) {
        return !this.hudHidden && (inside(x, y, this.panelX, this.panelY, this.panelW, this.panelH)
            || inside(x, y, this.cinemaX, this.cinemaY, this.cinemaW, this.cinemaH));
    }

    private static boolean inside(double mx, double my, int x, int y, int w, int h) {
        return mx >= x && mx < x + w && my >= y && my < y + h;
    }

    @Override
    public boolean mouseClicked(MouseButtonEvent event, boolean doubleClick) {
        double mx = event.x();
        double my = event.y();
        if (!this.hudHidden && insidePanel(mx, my)) {
            for (SliderRow slider : this.sliders) {
                if (slider.enabled().get() && inside(mx, my, slider.x(), slider.y(), slider.w(), ROW_H - 2)) {
                    this.activeSlider = slider;
                    applySliderMouse(slider, mx);
                    return true;
                }
            }
            for (ButtonBox button : this.buttons) {
                if (button.enabled().get() && inside(mx, my, button.x(), button.y(), button.w(), button.h())) {
                    button.action().run();
                    return true;
                }
            }
            return true;
        }

        if (event.button() == com.mojang.blaze3d.platform.InputConstants.MOUSE_BUTTON_LEFT
            || event.button() == com.mojang.blaze3d.platform.InputConstants.MOUSE_BUTTON_RIGHT) {
            this.viewportDragging = true;
            this.viewportMoved = false;
            this.pressX = mx;
            this.pressY = my;
            return true;
        }
        return super.mouseClicked(event, doubleClick);
    }

    @Override
    public boolean mouseDragged(MouseButtonEvent event, double dragX, double dragY) {
        if (this.activeSlider != null) {
            applySliderMouse(this.activeSlider, event.x());
            return true;
        }
        if (this.viewportDragging) {
            double distSq = (event.x() - this.pressX) * (event.x() - this.pressX)
                + (event.y() - this.pressY) * (event.y() - this.pressY);
            if (distSq > 9.0) {
                this.viewportMoved = true;
            }
            float sensitivity = 0.22f * (PhotoMode.fovDegrees() / 70.0f);
            PhotoMode.rotateCamera((float) dragX * sensitivity, (float) dragY * sensitivity);
            return true;
        }
        return super.mouseDragged(event, dragX, dragY);
    }

    @Override
    public boolean mouseReleased(MouseButtonEvent event) {
        if (this.activeSlider != null) {
            this.activeSlider = null;
            return true;
        }
        if (this.viewportDragging) {
            boolean clickedToFocus = !this.viewportMoved && event.button() == com.mojang.blaze3d.platform.InputConstants.MOUSE_BUTTON_LEFT
                && this.minecraft != null;
            this.viewportDragging = false;
            if (clickedToFocus) {
                PhotoMode.focusAtScreenPoint(this.minecraft, event.x(), event.y(), this.width, this.height);
            }
            return true;
        }
        return super.mouseReleased(event);
    }

    @Override
    public boolean mouseScrolled(double mouseX, double mouseY, double scrollX, double scrollY) {
        if (!insidePanel(mouseX, mouseY) && scrollY != 0.0) {
            if (VersionCompat.isKeyDown(InputConstants.KEY_LSHIFT) || VersionCompat.isKeyDown(InputConstants.KEY_RSHIFT)) {
                PhotoMode.setFovDegrees(PhotoMode.fovDegrees() - (float) scrollY * 2.5f);
            } else {
                PhotoMode.adjustMoveSpeed((float) scrollY);
            }
            return true;
        }
        return super.mouseScrolled(mouseX, mouseY, scrollX, scrollY);
    }

    private static void applySliderMouse(SliderRow slider, double mouseX) {
        float norm = Math.clamp((float) ((mouseX - slider.x()) / Math.max(1, slider.w())), 0.0f, 1.0f);
        slider.normSetter().accept(norm);
    }

    @Override
    public boolean keyPressed(KeyEvent event) {
        if (event.key() == InputConstants.KEY_H || event.key() == InputConstants.KEY_F1) {
            this.hudHidden = !this.hudHidden;
            return true;
        }
        if (event.key() == InputConstants.KEY_F2 && this.minecraft != null && !PhotoMode.isCapturePending()) {
            PhotoMode.requestSave(this.minecraft, true, false, true);
            return true;
        }
        if (event.key() == InputConstants.KEY_R && !VersionCompat.isKeyDown(InputConstants.KEY_LCONTROL)) {
            PhotoMode.resetParameters(this.minecraft);
            return true;
        }
        return super.keyPressed(event);
    }

    @Override
    public void onClose() {
        PhotoMode.exit();
        if (this.minecraft != null) {
            this.minecraft.gui.setScreen(this.parent);
        }
    }

    @Override
    public void removed() {
        PhotoMode.exit();
        super.removed();
    }
}
