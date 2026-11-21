package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.photo.PhotoCinema;
import com.g2806.radiante.client.photo.PhotoMode;
import com.g2806.radiante.client.photo.PhotoWatermark;
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
 * Interactive Photo Mode overlay screen: the live path-traced view between two panels. The player flies with WASD /
 * Space / Shift, holds a mouse button to look round (the cursor is held where it was pressed, as the game holds it
 * while playing) and clicks in the world to pull focus. The left panel is the photo: its frame, its scene (a time
 * of day and a weather of its own), how it is rendered and saved. The right panel is the camera: the look, the lens,
 * the focus and the exposure. Each panel is in named groups, and a button that steps through choices steps back with
 * the right button.
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
    // The photo's panel, on the left: framing, the scene, rendering and saving.
    private int cinemaX;
    private int cinemaY;
    private int cinemaW;
    private int cinemaH;

    private final List<SliderRow> sliders = new ArrayList<>();
    private final List<ButtonBox> buttons = new ArrayList<>();
    private SliderRow activeSlider;

    private final List<Header> headers = new ArrayList<>();

    private boolean viewportDragging;
    private boolean viewportMoved;
    /** Where the drag began, in the window's pixels: the cursor is put back there as it moves, and shown there after. */
    private double pressPixelX;
    private double pressPixelY;
    private double draggedDistance;

    /** A group's name over its controls. */
    private record Header(String key, int x, int y, int w) {
    }

    private record SliderRow(String labelKey, String tooltipKey, int x, int y, int w,
                             Supplier<Float> normGetter, Consumer<Float> normSetter,
                             Supplier<Component> valueText, Supplier<Boolean> enabled) {
    }

    /** back: what the right button does on a button that steps through choices (the one before), or null. */
    private record ButtonBox(Supplier<Component> label, String tooltipKey, int x, int y, int w, int h,
                             Runnable action, Runnable back, Supplier<Boolean> enabled, boolean accent) {
    }

    private static final int HEADER_H = 11;

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
        this.headers.clear();
        this.activeSlider = null;

        this.panelW = Math.min(212, Math.max(180, this.width / 4));
        int margin = 6;
        this.panelX = this.width - this.panelW - margin;
        this.panelY = margin;
        // On a short screen the groups lose their names so the panels still fit.
        boolean named = this.height >= 320;

        int innerX = this.panelX + PAD;
        int innerW = this.panelW - PAD * 2;
        int halfW = (innerW - 4) / 2;
        int y = this.panelY + 18;

        // The camera: a look sets the lens, the depth of field and the exposure together, and the groups below
        // fine-tune each.
        button(() -> Component.translatable("gui.radiante.photo.look", PhotoCinema.look() == null
                ? Component.translatable("gui.radiante.photo.preset.pick")
                : Component.translatable(PhotoCinema.look().key())),
            "gui.radiante.photo.look.tooltip", innerX, y, innerW,
            () -> PhotoCinema.cycleLook(1), () -> PhotoCinema.cycleLook(-1), () -> true, false);
        y += BUTTON_H + 4;

        y = header(named, "gui.radiante.photo.group.lens", innerX, y, innerW);
        button(() -> Component.translatable("gui.radiante.photo.lens", PhotoCinema.focalLabel()),
            "gui.radiante.photo.lens.tooltip", innerX, y, innerW,
            () -> PhotoCinema.cycleLens(1), () -> PhotoCinema.cycleLens(-1), () -> true, false);
        y += BUTTON_H + 4;
        addSlider("gui.radiante.photo.fov", "gui.radiante.photo.fov.tooltip", innerX, y, innerW,
            () -> (PhotoMode.fovDegrees() - 15.0f) / (120.0f - 15.0f),
            t -> PhotoMode.setFovDegrees(15.0f + t * (120.0f - 15.0f)),
            () -> Component.literal(Math.round(PhotoMode.fovDegrees()) + "° · " + PhotoCinema.focalLabel()),
            () -> true);
        y += ROW_H;
        addSlider("gui.radiante.photo.roll", "gui.radiante.photo.roll.tooltip", innerX, y, innerW,
            () -> (PhotoMode.rollDegrees() + 45.0f) / 90.0f,
            t -> PhotoMode.setRollDegrees(-45.0f + t * 90.0f),
            () -> Component.literal(String.format(Locale.ROOT, "%+.1f°", PhotoMode.rollDegrees())),
            () -> true);
        y += ROW_H + 2;

        y = header(named, "gui.radiante.photo.group.focus", innerX, y, innerW);
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
        y += ROW_H + 2;
        button(() -> PhotoMode.blades() < 3
                ? Component.translatable("gui.radiante.photo.blades.round")
                : Component.translatable("gui.radiante.photo.blades.count", PhotoMode.blades()),
            "gui.radiante.photo.blades.tooltip", innerX, y, innerW,
            PhotoMode::cycleBlades, null, () -> true, false);
        y += BUTTON_H + 4;
        addSlider("gui.radiante.photo.blade_angle", "gui.radiante.photo.blade_angle.tooltip", innerX, y, innerW,
            () -> PhotoMode.bladeRotationDegrees() / 180.0f,
            t -> PhotoMode.setBladeRotationDegrees(t * 180.0f),
            () -> Component.literal(Math.round(PhotoMode.bladeRotationDegrees()) + "°"),
            () -> PhotoMode.blades() >= 3 && PhotoMode.apertureRadius() >= 1.0e-4f);
        y += ROW_H + 2;

        addSlider("gui.radiante.photo.exposure", "gui.radiante.photo.exposure.tooltip", innerX, y, innerW,
            () -> (PhotoMode.exposureBias() + 4.0f) / 8.0f,
            t -> PhotoMode.setExposureBias(-4.0f + t * 8.0f),
            () -> Component.literal(String.format(Locale.ROOT, "%+.2f EV", PhotoMode.exposureBias())),
            () -> true);
        y += ROW_H + 4;

        // Reset / Hide UI / Close
        int thirdW = (innerW - 8) / 3;
        button(() -> Component.translatable("gui.radiante.photo.reset"), "gui.radiante.photo.reset.tooltip",
            innerX, y, thirdW, () -> PhotoMode.resetParameters(this.minecraft), null, () -> true, false);
        button(() -> Component.translatable("gui.radiante.photo.hide_ui"), "gui.radiante.photo.hide_ui.tooltip",
            innerX + thirdW + 4, y, thirdW, () -> this.hudHidden = true, null, () -> true, false);
        int closeX = innerX + (thirdW + 4) * 2;
        button(() -> Component.translatable("gui.done"), null, closeX, y, innerX + innerW - closeX, this::onClose,
            null, () -> true, false);
        y += BUTTON_H + PAD;

        this.panelH = y - this.panelY;
        initCinema(margin, named);
    }

    /** The photo's panel: the frame, the scene, how it is rendered and where it is saved. */
    private void initCinema(int margin, boolean named) {
        this.cinemaW = this.panelW;
        this.cinemaX = margin;
        this.cinemaY = margin;
        int innerX = this.cinemaX + PAD;
        int innerW = this.cinemaW - PAD * 2;
        int halfW = (innerW - 4) / 2;
        // Under the title, the sample count and its bar.
        int y = this.cinemaY + 36;
        Supplier<Boolean> inWorld = () -> this.minecraft != null && PhotoCinema.ownsWorld(this.minecraft);

        y = header(named, "gui.radiante.photo.group.framing", innerX, y, innerW);
        button(() -> Component.translatable("gui.radiante.photo.frame",
                Component.translatable(PhotoCinema.frame().label())),
            "gui.radiante.photo.frame.tooltip", innerX, y, halfW,
            () -> PhotoCinema.cycleFrame(1), () -> PhotoCinema.cycleFrame(-1), () -> true, false);
        button(() -> Component.translatable("gui.radiante.photo.guides."
                + PhotoCinema.guides().name().toLowerCase(Locale.ROOT)),
            "gui.radiante.photo.guides.tooltip", innerX + halfW + 4, y, innerW - halfW - 4,
            () -> PhotoCinema.cycleGuides(1), () -> PhotoCinema.cycleGuides(-1), () -> true, false);
        y += BUTTON_H + 4;
        button(() -> Component.translatable(PhotoWatermark.corner().key()), "gui.radiante.photo.logo.tooltip",
            innerX, y, innerW, () -> PhotoWatermark.cycle(1), () -> PhotoWatermark.cycle(-1), () -> true, false);
        y += BUTTON_H + 4;

        // A time of day and a weather for the photo alone, on a server as well (PhotoCinema).
        y = header(named, "gui.radiante.photo.group.scene", innerX, y, innerW);
        button(() -> Component.translatable("gui.radiante.photo.scene", PhotoCinema.scene() == null
                ? Component.translatable("gui.radiante.photo.preset.pick")
                : Component.translatable(PhotoCinema.scene().key())),
            "gui.radiante.photo.scene.tooltip", innerX, y, innerW,
            () -> PhotoCinema.cycleScene(this.minecraft, 1), () -> PhotoCinema.cycleScene(this.minecraft, -1),
            inWorld, false);
        y += BUTTON_H + 4;
        addSlider("gui.radiante.photo.time", "gui.radiante.photo.time.tooltip", innerX, y, innerW,
            () -> Math.max(0L, PhotoCinema.dayTime(this.minecraft)) / 24000.0f,
            t -> {
                // In quarters of an hour: every step starts the picture over.
                long ticks = Math.round(t * 24000.0) / 250L * 250L;
                if (Math.abs(ticks - PhotoCinema.dayTime(this.minecraft)) >= 250L) {
                    PhotoCinema.setDayTime(this.minecraft, ticks);
                }
            },
            () -> Component.literal(PhotoCinema.clock(Math.max(0L, PhotoCinema.dayTime(this.minecraft)))),
            inWorld);
        y += ROW_H + 2;
        button(() -> Component.translatable(PhotoCinema.weatherKey(this.minecraft)),
            "gui.radiante.photo.weather.tooltip", innerX, y, halfW,
            () -> PhotoCinema.cycleWeather(this.minecraft, 1), () -> PhotoCinema.cycleWeather(this.minecraft, -1),
            inWorld, false);
        button(() -> Component.translatable("gui.radiante.photo.scene.world"),
            "gui.radiante.photo.scene.world.tooltip", innerX + halfW + 4, y, innerW - halfW - 4,
            () -> PhotoCinema.resetScene(this.minecraft), null, PhotoCinema::sceneChanged, false);
        y += BUTTON_H + 4;

        y = header(named, "gui.radiante.photo.group.render", innerX, y, innerW);
        button(() -> Component.translatable("gui.radiante.photo.target_samples", PhotoMode.targetSamples()),
            "gui.radiante.photo.target_samples.tooltip", innerX, y, halfW,
            PhotoMode::cycleTargetSamples, null, () -> true, false);
        button(() -> Component.translatable(PhotoMode.hideHands()
                ? "gui.radiante.photo.hands.hidden" : "gui.radiante.photo.hands.shown"),
            "gui.radiante.photo.hands.tooltip", innerX + halfW + 4, y, innerW - halfW - 4,
            () -> PhotoMode.setHideHands(!PhotoMode.hideHands()), null, () -> true, false);
        y += BUTTON_H + 4;
        button(() -> Component.translatable(PhotoMode.denoise()
                ? "gui.radiante.photo.denoiser.on" : "gui.radiante.photo.denoiser.off"),
            "gui.radiante.photo.denoiser.tooltip", innerX, y, innerW,
            () -> PhotoMode.setDenoise(!PhotoMode.denoise()), null, () -> true, false);
        y += BUTTON_H + 4;

        y = header(named, "gui.radiante.photo.group.save", innerX, y, innerW);
        Supplier<Boolean> idle = () -> !PhotoMode.isCapturePending();
        button(() -> Component.translatable("gui.radiante.photo.save_png"), "gui.radiante.photo.save_png.tooltip",
            innerX, y, halfW, () -> save(true, false), null, idle, true);
        button(() -> Component.translatable("gui.radiante.photo.save_exr"), "gui.radiante.photo.save_exr.tooltip",
            innerX + halfW + 4, y, innerW - halfW - 4, () -> save(false, true), null, idle, true);
        y += BUTTON_H + 3;
        button(() -> Component.translatable("gui.radiante.photo.save_both"), "gui.radiante.photo.save_both.tooltip",
            innerX, y, halfW, () -> save(true, true), null, idle, true);
        button(() -> Component.translatable("gui.radiante.photo.open_folder"),
            "gui.radiante.photo.open_folder.tooltip", innerX + halfW + 4, y, innerW - halfW - 4,
            () -> {
                if (this.minecraft != null) {
                    Path dir = this.minecraft.gameDirectory.toPath().resolve("screenshots");
                    VersionCompat.openFolder(dir);
                }
            }, null, () -> true, false);
        y += BUTTON_H + PAD;
        this.cinemaH = y - this.cinemaY;
    }

    private void save(boolean png, boolean exr) {
        if (this.minecraft != null) {
            PhotoMode.requestSave(this.minecraft, png, exr, true);
        }
    }

    private void button(Supplier<Component> label, String tooltipKey, int x, int y, int w, Runnable action,
        Runnable back, Supplier<Boolean> enabled, boolean accent) {
        this.buttons.add(new ButtonBox(label, tooltipKey, x, y, w, BUTTON_H, action, back, enabled, accent));
    }

    /** A group's name at y; the y its first control starts at. */
    private int header(boolean named, String key, int x, int y, int w) {
        if (!named) {
            return y;
        }
        this.headers.add(new Header(key, x, y, w));
        return y + HEADER_H;
    }

    /** The part of the screen inside the chosen frame (Photo Mode's Frame), as x, y, width, height. */
    private int[] frameRect() {
        float aspect = PhotoCinema.frame().aspect();
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
        // The logo where the saved photo will have it (PhotoWatermark), with the controls hidden too: it is part of
        // the picture.
        if (PhotoWatermark.corner() != PhotoWatermark.Corner.OFF) {
            int[] at = PhotoWatermark.place(PhotoWatermark.corner(), r[2], r[3]);
            VersionCompat.blit(g, PhotoWatermark.TEXTURE, r[0] + at[0], r[1] + at[1], 0.0f, 0.0f, at[2], at[3],
                PhotoWatermark.TEXTURE_WIDTH, PhotoWatermark.TEXTURE_HEIGHT, PhotoWatermark.TEXTURE_WIDTH,
                PhotoWatermark.TEXTURE_HEIGHT);
        }
        if (!guides) {
            return;
        }
        int line = 0x70FFFFFF;
        int x = r[0];
        int y = r[1];
        int w = r[2];
        int h = r[3];
        switch (PhotoCinema.guides()) {
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
        g.text(this.font, Component.translatable("gui.radiante.photo.camera").withStyle(ChatFormatting.BOLD),
            this.panelX + PAD, this.panelY + 5, TEXT);

        // The photo's panel carries the title and how far the picture has come.
        int innerX = this.cinemaX + PAD;
        int innerW = this.cinemaW - PAD * 2;
        int headerY = this.cinemaY + 5;
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

        // The groups' names, each with a line running on to the panel's edge.
        for (Header header : this.headers) {
            Component name = Component.translatable(header.key());
            g.text(this.font, name, header.x(), header.y() + 1, TEXT_DIM);
            int lineX = header.x() + this.font.width(name) + 4;
            if (lineX < header.x() + header.w()) {
                g.fill(lineX, header.y() + 5, header.x() + header.w(), header.y() + 6, 0x30FFFFFF);
            }
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
        int hintLeft = this.cinemaX + this.cinemaW + 6;
        int maxHintRoom = this.panelX - 6 - hintLeft;
        if (maxHintRoom > 80) {
            String rawHint = Component.translatable("gui.radiante.photo.controls_hint").getString();
            if (this.font.width(rawHint) > maxHintRoom - 12) {
                rawHint = this.font.plainSubstrByWidth(rawHint, Math.max(0, maxHintRoom - 18)) + "…";
            }
            int hintW = this.font.width(rawHint) + 12;
            int hintY = this.height - 18;
            g.fill(hintLeft, hintY, hintLeft + hintW, hintY + 13, PANEL);
            g.text(this.font, Component.literal(rawHint), hintLeft + 6, hintY + 3, TEXT_DIM);
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
        // Top centre, clear of both panels.
        int w = this.font.width(text) + 16;
        int x = (this.width - w) / 2;
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
                    boolean right = event.button() == InputConstants.MOUSE_BUTTON_RIGHT;
                    if (right && button.back() != null) {
                        button.back().run();
                    } else if (!right) {
                        button.action().run();
                    }
                    return true;
                }
            }
            return true;
        }

        if (event.button() == InputConstants.MOUSE_BUTTON_LEFT || event.button() == InputConstants.MOUSE_BUTTON_RIGHT) {
            this.viewportDragging = true;
            this.viewportMoved = false;
            this.draggedDistance = 0.0;
            // The cursor is hidden and held where it was pressed while the view turns, as the game holds it while
            // playing: it used to run to the edge of the window, where the turn stopped, or out of it.
            if (this.minecraft != null) {
                var mouse = (com.g2806.radiante.mixin.gui.MouseHandlerAccessor) this.minecraft.mouseHandler;
                this.pressPixelX = mouse.radiante$x();
                this.pressPixelY = mouse.radiante$y();
                VersionCompat.showCursor(false);
            }
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
            this.draggedDistance += Math.abs(dragX) + Math.abs(dragY);
            if (this.draggedDistance > 3.0) {
                this.viewportMoved = true;
            }
            float sensitivity = 0.22f * (PhotoMode.fovDegrees() / 70.0f);
            PhotoMode.rotateCamera((float) dragX * sensitivity, (float) dragY * sensitivity);
            if (this.minecraft != null) {
                VersionCompat.warpCursor(this.minecraft, this.pressPixelX, this.pressPixelY);
            }
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
            boolean clickedToFocus = !this.viewportMoved && event.button() == InputConstants.MOUSE_BUTTON_LEFT
                && this.minecraft != null;
            releaseView();
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

    /** The cursor shown again, where the drag began. */
    private void releaseView() {
        if (this.viewportDragging) {
            this.viewportDragging = false;
            VersionCompat.showCursor(true);
        }
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
        releaseView();
        PhotoMode.exit();
        super.removed();
    }
}
