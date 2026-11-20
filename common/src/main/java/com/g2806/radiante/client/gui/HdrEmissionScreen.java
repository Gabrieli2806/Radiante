package com.g2806.radiante.client.gui;

import com.g2806.radiante.client.option.EmissionColor;
import com.g2806.radiante.compat.VersionCompat;
import java.util.Locale;
import java.util.function.Consumer;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.components.AbstractSliderButton;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.EditBox;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

/** An HDR tint is a linear RGB colour and exposure; the numeric fields also accept channels above one directly. */
final class HdrEmissionScreen extends VersionCompat.DrawnScreen {
    private static final String KEY = "options.radiante.emission_colour.";
    private final Screen parent;
    private final Consumer<EmissionColor> save;
    private EmissionColor colour;
    private final double[] channels = new double[3];
    private double exposure;
    private final EditBox[] fields = new EditBox[3];
    private final ChannelSlider[] sliders = new ChannelSlider[4];
    private Button done;
    private boolean updating;
    /** The block's textures, all of which the colour applies to; the first is the preview. */
    private final java.util.List<String> sprites;

    HdrEmissionScreen(Screen parent, Component title, EmissionColor colour, Consumer<EmissionColor> save,
        java.util.List<String> sprites) {
        super(title);
        this.parent = parent;
        this.save = save;
        this.colour = colour;
        this.sprites = sprites;
        decompose();
    }

    private void decompose() {
        double gain = Math.max(1, colour.peak());
        exposure = Math.log(gain) / Math.log(2);
        channels[0] = colour.red() / gain;
        channels[1] = colour.green() / gain;
        channels[2] = colour.blue() / gain;
    }

    @Override
    protected void init() {
        int left = width / 2 - 150;
        int top = Math.max(45, height / 2 - 78);
        for (int i = 0; i < 3; i++) {
            final int channel = i;
            fields[i] = new EditBox(font, left + 220, top + i * 28, 80, 20, label(i));
            fields[i].setMaxLength(14);
            fields[i].setValue(Float.toString(component(i)));
            fields[i].setResponder(text -> readFields());
            addRenderableWidget(fields[i]);
            sliders[i] = addRenderableWidget(new ChannelSlider(left, top + i * 28, 210, i));
        }
        sliders[3] = addRenderableWidget(new ChannelSlider(left, top + 88, 300, 3));
        addRenderableWidget(Button.builder(Component.translatable("controls.reset"), button -> {
            colour = EmissionColor.WHITE; decompose(); refresh();
        }).bounds(left, top + 120, 94, 20).build());
        addRenderableWidget(Button.builder(Component.translatable("gui.cancel"), button -> onClose())
            .bounds(left + 103, top + 120, 94, 20).build());
        done = addRenderableWidget(Button.builder(Component.translatable("gui.done"), button -> {
            save.accept(colour); onClose();
        }).bounds(left + 206, top + 120, 94, 20).build());
    }

    private static Component label(int channel) {
        return Component.translatable(KEY + new String[] {"red", "green", "blue", "exposure"}[channel]);
    }

    private float component(int channel) {
        return switch (channel) { case 0 -> colour.red(); case 1 -> colour.green(); default -> colour.blue(); };
    }

    private void readFields() {
        if (updating || done == null) return;
        try {
            float[] rgb = new float[3];
            for (int i = 0; i < 3; i++) {
                rgb[i] = Float.parseFloat(fields[i].getValue());
                if (!Float.isFinite(rgb[i]) || rgb[i] < 0 || rgb[i] > EmissionColor.MAX) throw new NumberFormatException();
            }
            colour = new EmissionColor(rgb[0], rgb[1], rgb[2]);
            decompose();
            for (ChannelSlider slider : sliders) slider.sync();
            done.active = true;
        } catch (NumberFormatException invalid) {
            done.active = false;
        }
    }

    private void refresh() {
        updating = true;
        for (int i = 0; i < 3; i++) fields[i].setValue(String.format(Locale.ROOT, "%.6g", component(i)));
        for (ChannelSlider slider : sliders) slider.sync();
        if (done != null) done.active = true;
        updating = false;
    }

    private final class ChannelSlider extends AbstractSliderButton {
        private final int channel;
        ChannelSlider(int x, int y, int width, int channel) {
            super(x, y, width, 20, Component.empty(), channel == 3 ? (exposure + 12) / 28 : channels[channel]);
            this.channel = channel;
            updateMessage();
        }
        void sync() { value = channel == 3 ? (exposure + 12) / 28 : channels[channel]; updateMessage(); }
        @Override protected void updateMessage() {
            String text = channel == 3 ? String.format(Locale.ROOT, "%+.2f EV", exposure)
                : String.format(Locale.ROOT, "%.3f", channels[channel]);
            setMessage(Component.empty().append(label(channel)).append(": " + text));
        }
        @Override protected void applyValue() {
            if (channel == 3) exposure = value * 28 - 12;
            else channels[channel] = value;
            double gain = Math.pow(2, exposure);
            colour = new EmissionColor((float) (channels[0] * gain), (float) (channels[1] * gain), (float) (channels[2] * gain));
            refresh();
        }
    }

    @Override
    protected void extractContents(GuiGraphicsExtractor g, int mouseX, int mouseY) {
        int top = Math.max(45, height / 2 - 78);
        g.centeredText(font, title, width / 2, top - 34, 0xFFFFFFFF);
        g.centeredText(font, Component.translatable(KEY + "hint"), width / 2, top - 20, 0xFFB0B8B4);
        // The preview: the block's texture as it is, and the light it casts with this colour, a pool of it on a
        // grey floor that fades with the distance as the light does, brighter with the exposure.
        int px = width / 2 + 158;
        int previewW = 76;
        var sprite = sprites.isEmpty() ? null : BlockEmissionSettings.sprite(sprites.get(0));
        if (sprite != null) {
            VersionCompat.blitAtlasSprite(g, sprite, px, top, 32, 32, 0xFFFFFFFF);
        } else {
            g.fill(px, top, px + 32, top + 32, 0xFF303030);
        }
        g.text(font, Component.translatable(KEY + "texture"), px + 36, top + 12, 0xFFB0B8B4);
        int floorY = top + 40;
        g.fill(px, floorY, px + previewW, floorY + 36, 0xFF202020);
        // The pool brighter with the exposure, and its colour as the light's own (not scaled to white), so a
        // stronger light reads stronger here too.
        float gain = (float) Math.min(Math.pow(2.0, exposure), 4.0);
        for (int ring = 6; ring >= 1; ring--) {
            float falloff = ring / 6.0f;
            float strength = Math.min(1.0f, (1.0f - falloff * 0.8f) * gain * 0.9f);
            int w = Math.round(previewW * falloff);
            int h = Math.round(36 * falloff);
            int x0 = px + (previewW - w) / 2;
            int y0 = floorY + (36 - h) / 2;
            g.fill(x0, y0, x0 + w, y0 + h, scaled(colour.preview(), strength));
        }
        if (sprite != null) {
            VersionCompat.blitAtlasSprite(g, sprite, px + previewW / 2 - 8, floorY + 10, 16, 16, 0xFFFFFFFF);
        }
        g.text(font, Component.translatable(KEY + "light"), px, floorY + 40, 0xFFB0B8B4);
        if (sprites.size() > 1) {
            g.text(font, Component.translatable(KEY + "textures", sprites.size()), px, floorY + 52, 0xFF808888);
        }
    }

    /** An opaque colour darkened towards black by strength (0 black, 1 as it is). */
    private static int scaled(int argb, float strength) {
        int r = Math.round(((argb >> 16) & 255) * strength);
        int gr = Math.round(((argb >> 8) & 255) * strength);
        int b = Math.round((argb & 255) * strength);
        return 0xFF000000 | r << 16 | gr << 8 | b;
    }

    @Override public void onClose() { minecraft.gui.setScreen(parent); }
}
