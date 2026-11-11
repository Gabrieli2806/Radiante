package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.option.Options;
import com.g2806.radiante.client.proxy.vulkan.HdrProxy;
import com.g2806.radiante.compat.VersionCompat;
import java.util.function.BiConsumer;
import java.util.function.IntConsumer;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.AbstractSliderButton;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.util.FormattedCharSequence;

/**
 * A guided way to set HDR Peak Brightness and HDR Paper White: the same two settings as the sliders in the Image
 * section, and in the same nits. The pictures are ordinary screenshots in textures/gui/hdr_calibration, shown through
 * the HDR composition (HdrProxy.setCalibration), since nothing the interface draws goes above paper white.
 */
final class HdrCalibrationScreen extends VersionCompat.DrawnScreen {
    private static final String KEY = "options.radiante.hdr_calibration.";
    private static final Identifier DARK =
        Identifier.fromNamespaceAndPath("radiante", "textures/gui/hdr_calibration/dark.png");
    private static final Identifier BRIGHT =
        Identifier.fromNamespaceAndPath("radiante", "textures/gui/hdr_calibration/bright.png");
    private static final int PAGE_PEAK = 0;
    private static final int PAGE_PAPER_WHITE = 1;
    private static final int PAGE_CHECK = 2;
    private static final int MODE_OFF = 0;
    private static final int MODE_PEAK_PATTERN = 1;
    private static final int MODE_PICTURES = 2;
    private static final int CONTENT_TOP = 54;

    private final Screen parent;
    private final BiConsumer<Integer, Integer> save;
    private final int oldPeak = Options.hdrPeakNits;
    private final int oldPaperWhite = Options.hdrPaperWhiteNits;
    private int page = PAGE_PEAK;

    /** Called with the peak and paper white on Done. The present reads Options, so they change live while here. */
    HdrCalibrationScreen(Screen parent, int peak, int paperWhite, BiConsumer<Integer, Integer> save) {
        super(Component.translatable(KEY + "title"));
        this.parent = parent;
        this.save = save;
        Options.hdrPeakNits = Math.clamp(peak, 400, 4000);
        Options.hdrPaperWhiteNits = Math.clamp(paperWhite, 80, 400);
    }

    @Override
    protected void init() {
        int left = width / 2 - 150;
        int buttons = height - 28;
        if (page == PAGE_PEAK) {
            addRenderableWidget(new NitsSlider(left, buttons - 26, "options.radiante.hdr_peak", 400, 4000, 10,
                Options.hdrPeakNits, value -> Options.hdrPeakNits = value));
        } else if (page == PAGE_PAPER_WHITE) {
            addRenderableWidget(new NitsSlider(left, buttons - 26, "options.radiante.hdr_paper_white", 80, 400, 5,
                Options.hdrPaperWhiteNits, value -> Options.hdrPaperWhiteNits = value));
        }
        addRenderableWidget(Button.builder(Component.translatable("gui.cancel"), button -> onClose())
            .bounds(left, buttons, 94, 20).build());
        Button back = addRenderableWidget(Button.builder(Component.translatable("gui.back"), button -> turnTo(page - 1))
            .bounds(left + 103, buttons, 94, 20).build());
        back.active = page > PAGE_PEAK;
        addRenderableWidget(Button.builder(Component.translatable(page == PAGE_CHECK ? "gui.done" : KEY + "next"),
            button -> {
                if (page == PAGE_CHECK) {
                    int peak = Options.hdrPeakNits;
                    int paperWhite = Options.hdrPaperWhiteNits;
                    restore();
                    save.accept(peak, paperWhite);
                } else {
                    turnTo(page + 1);
                }
            }).bounds(left + 206, buttons, 94, 20).build());
    }

    private void turnTo(int target) {
        page = target;
        rebuildWidgets();
    }

    @Override
    protected boolean extractOwnBackground(GuiGraphicsExtractor g) {
        // Black, as what is around a brightness test changes what the eye makes of it.
        g.fill(0, 0, width, height, 0xFF000000);
        return true;
    }

    @Override
    protected void extractContents(GuiGraphicsExtractor g, int mouseX, int mouseY) {
        g.centeredText(font, title, width / 2, 10, 0xFFFFFFFF);
        int y = 26;
        for (FormattedCharSequence line : font.split(Component.translatable(KEY + "hint." + page),
            Math.min(width - 32, 420))) {
            g.centeredText(font, line, width / 2, y, Theme.TEXT_DIM);
            y += font.lineHeight + 1;
        }
        int bottom = height - (page == PAGE_CHECK ? 48 : 64);
        int room = Math.max(bottom - CONTENT_TOP, 16);
        if (page == PAGE_PEAK) {
            int side = Math.min(room, 140);
            int x = (width - side) / 2;
            int top = CONTENT_TOP + (room - side) / 2;
            setCalibration(MODE_PEAK_PATTERN, x, top, side, side, 0, 0, 0, 0);
        } else if (page == PAGE_PAPER_WHITE) {
            int pictureWidth = Math.min((width - 36) / 2, room * 16 / 9);
            int pictureHeight = pictureWidth * 9 / 16;
            int top = CONTENT_TOP + (room - pictureHeight) / 2;
            int darkX = width / 2 - 4 - pictureWidth;
            int brightX = width / 2 + 4;
            picture(g, DARK, darkX, top, pictureWidth, pictureHeight);
            picture(g, BRIGHT, brightX, top, pictureWidth, pictureHeight);
            setCalibration(MODE_PICTURES, darkX, top, pictureWidth, pictureHeight, brightX, top, pictureWidth,
                pictureHeight);
        } else {
            int pictureHeight = Math.min(room - 12, (width - 32) * 9 / 16);
            int pictureWidth = pictureHeight * 16 / 9;
            int x = (width - pictureWidth) / 2;
            picture(g, BRIGHT, x, CONTENT_TOP, pictureWidth, pictureHeight);
            setCalibration(MODE_PICTURES, x, CONTENT_TOP, pictureWidth, pictureHeight, 0, 0, 0, 0);
            g.centeredText(font, Component.translatable(KEY + "values", Options.hdrPeakNits, Options.hdrPaperWhiteNits),
                width / 2, CONTENT_TOP + pictureHeight + 3, 0xFFFFFFFF);
        }
    }

    private static void picture(GuiGraphicsExtractor g, Identifier texture, int x, int y, int w, int h) {
        VersionCompat.blit(g, texture, x, y, 0.0f, 0.0f, w, h, w, h);
    }

    private void setCalibration(int mode, int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
        float w = width;
        float h = height;
        HdrProxy.setCalibration(mode, ax / w, ay / h, aw / w, ah / h, bx / w, by / h, bw / w, bh / h);
    }

    private void restore() {
        Options.hdrPeakNits = oldPeak;
        Options.hdrPaperWhiteNits = oldPaperWhite;
    }

    @Override
    public void removed() {
        HdrProxy.setCalibration(MODE_OFF, 0, 0, 0, 0, 0, 0, 0, 0);
        super.removed();
    }

    @Override
    public void onClose() {
        restore();
        save.accept(null, null);
    }

    private static final class NitsSlider extends AbstractSliderButton {
        private final String key;
        private final int min;
        private final int max;
        private final int step;
        private final IntConsumer onChange;

        NitsSlider(int x, int y, String key, int min, int max, int step, int current, IntConsumer onChange) {
            super(x, y, 300, 20, Component.empty(), (current - min) / (double) (max - min));
            this.key = key;
            this.min = min;
            this.max = max;
            this.step = step;
            this.onChange = onChange;
            updateMessage();
        }

        private int nits() {
            return Math.clamp(min + Math.round((float) (value * (max - min)) / step) * step, min, max);
        }

        @Override
        protected void updateMessage() {
            setMessage(Component.translatable("options.radiante.nits_value", Component.translatable(key), nits()));
        }

        @Override
        protected void applyValue() {
            onChange.accept(nits());
        }
    }
}
